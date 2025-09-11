/* SPDX-License-Identifier: 0BSD
 * Copyright (C) 2025 наб (nabijaczleweli@nabijaczleweli.xyz)
 */

#include "libvoreutils.hpp"
#include <atomic>
#include <chrono>
#include <type_traits>

namespace {
	struct bundle {
		// All-0 is a valid initial state for this, so ftruncate() is a valid initialiser
		using shm_t = std::atomic<bundle>;

		static const constexpr std::size_t PIDBITS    = 22;  // 2^22 is the current max max (but any max is ok)
		static const constexpr std::size_t SUBSECBITS = 64 - 32 - PIDBITS;
		std::uint64_t last_winner : PIDBITS;   // the process that was dispatched
		std::uint64_t last_time_won_sec : 32;  // last time a process was dispatched
		std::uint64_t last_time_won_subsec : SUBSECBITS;
		using subsec = std::chrono::duration<std::uint64_t, std::ratio<1, 1llu << SUBSECBITS>>;

		bundle(pid_t pid, const struct timespec & now)
		      : last_winner(0), last_time_won_sec(now.tv_sec),
		        last_time_won_subsec(std::chrono::duration_cast<subsec>(std::chrono::nanoseconds{now.tv_nsec}).count()) {
			std::make_unsigned_t<pid_t> pidbits = pid;
			for(int left = sizeof(pidbits) * 8; left > 0; left -= PIDBITS, pidbits >>= PIDBITS)
				this->last_winner ^= pidbits & ((1u << PIDBITS) - 1);
		}

		operator struct timespec() const {
			return {static_cast<time_t>(this->last_time_won_sec),
			        static_cast<int>(std::chrono::duration_cast<std::chrono::nanoseconds>(subsec{this->last_time_won_subsec}).count())};
		}

		static auto map(const char * self, shm_t *& shm) -> int {
			vore::file::fd<false> shm_f{shm_open("/" SHMEM_FILE, O_RDWR | O_CREAT, 0644)};
			if(shm_f == -1)
				return std::fprintf(stderr, "%s: %s: %s\n", self, "/" SHMEM_FILE, std::strerror(errno)), 1;

			if(ftruncate(shm_f, sizeof(shm_t)))
				return std::fprintf(stderr, "%s: %s\n", self, std::strerror(errno)), 1;

			shm = static_cast<shm_t *>(mmap(nullptr, sizeof(shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_f, 0));
			if(shm == MAP_FAILED)
				return std::fprintf(stderr, "%s: %s\n", self, std::strerror(errno)), 1;

			return 0;
		}
	};
	static_assert(sizeof(bundle) == sizeof(std::uint64_t));

	using shm_t = bundle::shm_t;
}
