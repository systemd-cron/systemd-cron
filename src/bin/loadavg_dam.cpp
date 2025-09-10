/* SPDX-License-Identifier: 0BSD
 * Copyright (C) 2025 наб (nabijaczleweli@nabijaczleweli.xyz)
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

#define SHMEM_FILE "systemd-cron.loadavg_dam"
#include "shmemutil.hpp"


auto main(int, const char * const * argv) -> int {
	const auto self = *argv++;
	if(*argv && *argv == "--"sv)
		++argv;
	if(!*argv || *(argv + 1))
		return std::fprintf(stderr, "usage: %s below-loadavg\n", self), 1;

	double target;
	if(auto err = vore::parse_floating(*argv, target); err && ((target <= 0) && (err = "<= 0")))
		return std::fprintf(stderr, "%s: %s: %s\n", self, *argv, err), 1;


	shm_t * shm;
	if(auto err = bundle::map(self, shm))
		return err;

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
