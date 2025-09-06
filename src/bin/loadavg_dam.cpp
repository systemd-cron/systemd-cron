/* Copyright (C) 2025 наб (nabijaczleweli@nabijaczleweli.xyz)
 * SPDX-License-Identifier: 0BSD
 *
 * For jobs like {below_loadavg: double, spawn: void()}, this executes
 *   for(;;) {
 *     sleep(30);
 *     while(!(j = find_random_if(jobs, j => j.below_loadavg < getloadavg()))
 *       sleep(5);
 *     j.spawn();
 *   }
 * via the swarm of pending jobs.
 *
 * lodavg_dam targets ExecStartPre=, so spawn=exit(3).
 */

#include "libvoreutils.hpp"
#include <atomic>
#include <type_traits>


namespace {
	struct bundle {
		static const constexpr std::size_t PIDBITS    = 22;  // 2^22 is the current max max (but any max is ok)
		static const constexpr std::size_t SUBSECBITS = 64 - 32 - PIDBITS;
		std::uint64_t last_winner : PIDBITS;   // the process that was dispatched
		std::uint64_t last_time_won_sec : 32;  // last time a process was dispatched
		std::uint64_t last_time_won_subsec : SUBSECBITS;

		bundle(pid_t pid, const struct timespec & now)
		      : last_winner(0), last_time_won_sec(now.tv_sec),
		        last_time_won_subsec(static_cast<std::uint64_t>(now.tv_nsec) * (1llu << SUBSECBITS) / 1'000'000'000) {
			std::make_unsigned_t<pid_t> pidbits = pid;
			for(int left = sizeof(pidbits) * 8; left > 0; left -= PIDBITS, pidbits >>= PIDBITS)
				this->last_winner ^= pidbits & ((1u << PIDBITS) - 1);
		}

		operator struct timespec() const { return {this->last_time_won_sec, this->last_time_won_subsec}; }
	};
	static_assert(sizeof(bundle) == sizeof(std::uint64_t));

	// All-0 is a valid initial state for this, so ftruncate() is a valid initialiser
	using shm_t = std::atomic<struct bundle>;
}


auto main(int, const char * const * argv) -> int {
	const auto self = *argv++;
	if(*argv && *argv == "--"sv)
		++argv;
	if(!*argv || *(argv + 1))
		return std::fprintf(stderr, "usage: %s below-loadavg\n", self), 1;

	double target;
	if(auto err = vore::parse_floating(*argv, target))
		return std::fprintf(stderr, "%s: %s: %s\n", self, *argv, err), 1;
	if(target <= 0)
		return std::fprintf(stderr, "%s: %s: %s\n", self, *argv, "<= 0"), 1;


	auto shm_f = shm_open("/systemd-cron", O_RDWR | O_CREAT, 0644);
	if(shm_f == -1)
		return std::fprintf(stderr, "%s: %s: %s\n", self, "/systemd-cron", std::strerror(errno)), 1;

	if(ftruncate(shm_f, sizeof(shm_t)))
		return std::fprintf(stderr, "%s: %s\n", self, std::strerror(errno)), 1;

	auto shm = static_cast<shm_t *>(mmap(nullptr, sizeof(shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_f, 0));
	if(shm == MAP_FAILED)
		return std::fprintf(stderr, "%s: %s\n", self, std::strerror(errno)), 1;

	close(shm_f);


	for(const auto pid = getpid();; sleep(5)) {
		const auto last = shm->load();

		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
		if((now - last) > (struct timespec){30, 0}) {
			if(double load; getloadavg(&load, 1) != 1 || load > target)
				continue;  // load too high

			if(auto value = last; !shm->compare_exchange_strong(value, {pid, now}))
				continue;  // we lost

			break;
		}
	}
}
