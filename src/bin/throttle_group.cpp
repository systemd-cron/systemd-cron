/* SPDX-License-Identifier: 0BSD
 * Copyright (C) 2025 наб (nabijaczleweli@nabijaczleweli.xyz)
 *
 * Talk to systemd over org.freedesktop.systemd1(5) to:
 * 1. find other jobs matching "cron-*.service" with ExecStartPre=argv[0] argv[1] [...]
 * 2. sleep until 5 minutes after the last such job exited
 * 3. atomically consume the current job slot, exit
 *
 * See https://github.com/systemd-cron/systemd-cron/issues/173#issuecomment-3261758772 for demo and diagram.
 *
 * self_object := $(GetUnitByPID $$)
 * ListUnitsByPatterns("cron-*.service")
 * | filter(load_state = "loaded")
 * | filter(name !~ "@")                                       # filter out cron-mail@*
 * | filter(object != $self_object)                            # deadlock against self
 * | map(object.get-property(ExecStartPre)) &
 * | filter(ExecStartPre.argv ~ ^[$0, $group, ...])
 * if((active_state = "activating" && active_sub_state != "start-pre") || # Type=oneshot is always activating and never active
 * | (                                                                    # activating + start-pre means running ExecStartPre=,
 * | (                                                                    # which we need to exclude to not deadlock against other throttle_groups
 * | (active_state ∈ {"active", "reloading"})                  # reloading is a special type of active
 * | | sleep(BLANKING_INTERVAL)                                # a unit from our group is currently running
 * | ^ short-circuit back to top
 * |
 * | map(object.get-property(ExecMainStartTimestampMonotonic)) &
 * | map(object.get-property(ExecMainExitTimestampMonotonic))
 * if(ExecMainStartTimestampMonotonic > ExecMainExitTimestampMonotonic)
 * | | sleep(BLANKING_INTERVAL)                                # a unit from our group is currently running (race)
 * | ^ short-circuit back to top
 * |
 * | map(min_sleep := BLANKING_INTERVAL - (now - ExecMainExitTimestampMonotonic))
 * goal_sleep := reduce(max(min_sleep, 0))
 *
 * if(goal_sleep > 0)
 *   | sleep(goal_sleep)                                       # a unit from our group was running less than 5 minutes ago, sleep out the remaining 5 minutes
 *   ^ back to top
 *
 * if(!cmpxchg("/systemd-cron.throttle_group"))
 *   | sleep(5s)                                               # a different throttle_group instance loosed a job while we were looking
 *   ^ back to top
 *
 * exit to loose job
 *
 * All get-property queries are scatter-gather, and a short-circuit cancels outstanding orders.
 */
#if 0 /* clang-format off */
[ $# -eq 1 ] || {
	printf 'usage: %s group\n' "$0" >&2
	exit 1
}
group="$1"

# org.freedesktop.systemd1(5):
#   ListUnitsByPatterns("cron-*.service")
#   | filter(load_state="loaded")
#   | filter(name !~ "@")                  # filter out cron-mail@*
#   | flat_map(object.get-property(ExecMain{Start,Exit}TimestampMonotonic, ExecStartPre))
#   | filter(ExecStartPre ~ '"$0" "$group"')
#   | find(ExecStartPre ~ '"$0" "$group"')
busctl --system call org.freedesktop.systemd1 /org/freedesktop/systemd1 org.freedesktop.systemd1.Manager ListUnitsByPatterns asas 0 1 'cron-*.service' |
	awk '{gsub(/\\"/, ""); gsub(/" ([0-9]*) "/, "\" \""); gsub(/" "/, "\"\n\""); gsub(/"/, ""); print}' |
	while read -r name && read -r description && read -r load_state && read -r active_state && read -r active_sub_state && read -r following && read -r object && read -r job_type && read -r job_path; do
		[ "$load_state" = "loaded" ] || continue
		[ "${name%@*}"  = "$name"  ] || continue  # first name may have garbage on front (doesn''t matter for this)
		busctl --system get-property org.freedesktop.systemd1 "$object" org.freedesktop.systemd1.Service ExecMainStartTimestampMonotonic ExecMainExitTimestampMonotonic ExecStartPre | paste -s &
	done | while read -r _ ExecMainStartTimestampMonotonic _ ExecMainExitTimestampMonotonic _ ExecStartPre; do
		[ "${ExecStartPre%"\"$0\" \"$group\""*}" != "$ExecStartPre" ] || continue  # "
		echo ExecMainStartTimestampMonotonic=$ExecMainStartTimestampMonotonic
		echo ExecMainExitTimestampMonotonic =$ExecMainExitTimestampMonotonic
	done
#endif /* clang-format on */

#define SHMEM_FILE "systemd-cron.throttle_group"
#include "shmemutil.hpp"
#include <forward_list>
#include <memory>
#include <numeric>
#include <systemd/sd-bus.h>


namespace {
	const constexpr struct timespec BLANKING_INTERVAL{5 * 60, 0};


	auto sd_bus_get_property_async(sd_bus * bus, sd_bus_slot ** slot, const char * destination, const char * path, const char * interface, const char * member,
	                               sd_bus_message_handler_t callback, void * userdata) -> int {
		return sd_bus_call_method_async(bus, slot, destination, path, "org.freedesktop.DBus.Properties", "Get", callback, userdata, "ss", interface, member);
	}

	struct sd_bus_message_deleter {
		void operator()(sd_bus_message * m) const noexcept { sd_bus_message_unref(m); }
	};

	struct sd_bus_slot_deleter {
		void operator()(sd_bus_slot * m) const noexcept { sd_bus_slot_unref(m); }
	};


	std::string_view self, group;
	sd_bus * bus;
	struct timespec now;
	struct listed_unit * winner;
	struct listed_unit {
		const char *object, *active_state, *active_sub_state;
		std::unique_ptr<sd_bus_slot, sd_bus_slot_deleter> slot, ExecMainStartTimestampMonotonic_slot, ExecMainExitTimestampMonotonic_slot;
		std::optional<struct timespec> ExecMainStartTimestampMonotonic, ExecMainExitTimestampMonotonic;
		struct timespec result;

		auto ExecStartPre(sd_bus_message * m) -> int {
			if(sd_bus_message_enter_container(m, 'v', "a(sasbttttuii)") < 0 || sd_bus_message_enter_container(m, 'a', "(sasbttttuii)") < 0)
				return -1;

			while(!winner && !this->ExecMainStartTimestampMonotonic_slot && !this->ExecMainExitTimestampMonotonic_slot) {
				const char * s;
				if(sd_bus_message_enter_container(m, 'r', "sasbttttuii") < 0 || sd_bus_message_read(m, "s", &s) <= 0 || sd_bus_message_enter_container(m, 'a', "s") < 0)
					return -1;

				if(const char * argv[2]; sd_bus_message_read(m, "s", &argv[0]) > 0 && sd_bus_message_read(m, "s", &argv[1]) > 0) {
					if(argv[1] == "--"sv)
						if(sd_bus_message_read(m, "s", &argv[1]) <= 0)
							goto invalid;
					if(argv[0] == self && argv[1] == group) {
						if((this->active_state == "activating"sv && this->active_sub_state != "start-pre"sv) ||  //
						   this->active_state == "active"sv || this->active_state == "reloading"sv) {
							this->result = BLANKING_INTERVAL;
							winner       = this;
						} else {
#define EXEC_MAIN_TIMESTAMP_MONOTONIC(which)                                                                                              \
	sd_bus_slot * which{};                                                                                                                  \
	sd_bus_get_property_async(                                                                                                              \
	    bus, &which, "org.freedesktop.systemd1", this->object, "org.freedesktop.systemd1.Service", "ExecMain" #which "TimestampMonotonic",  \
	    [](sd_bus_message * m, void * userdata, sd_bus_error *) {                                                                           \
		    return static_cast<listed_unit *>(userdata)->ExecMainOneTimestampMonotonic(m, &listed_unit::ExecMain##which##TimestampMonotonic); \
	    },                                                                                                                                  \
	    this);                                                                                                                              \
	this->ExecMain##which##TimestampMonotonic_slot.reset(which)
							EXEC_MAIN_TIMESTAMP_MONOTONIC(Start);
							EXEC_MAIN_TIMESTAMP_MONOTONIC(Exit);
						}
					}

				invalid:;
					while(sd_bus_message_read(m, "s", &s) == -EBUSY)
						;
				}

				if(sd_bus_message_exit_container(m) <= 0 || sd_bus_message_skip(m, "bttttuii") <= 0 || sd_bus_message_exit_container(m) <= 0)
					return -1;
			}

			return 0;
		}

		auto ExecMainOneTimestampMonotonic(sd_bus_message * m, std::optional<struct timespec> listed_unit::* member) -> int {
			std::uint64_t usec;
			if(sd_bus_message_enter_container(m, 'v', "t") < 0 || sd_bus_message_read(m, "t", &usec) < 0)
				return -1;

			auto sec      = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::microseconds{usec});
			this->*member = {static_cast<time_t>(sec.count()),
			                 static_cast<int>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::microseconds{usec} - sec).count())};

			if(this->ExecMainStartTimestampMonotonic && this->ExecMainExitTimestampMonotonic) {
				if(*this->ExecMainStartTimestampMonotonic > *this->ExecMainExitTimestampMonotonic) {  // should've been caught by active_state check
					this->result = BLANKING_INTERVAL;
					winner       = this;
				} else
					this->result = BLANKING_INTERVAL - (now - *this->ExecMainExitTimestampMonotonic);
			}
			return 0;
		}
	};
}


auto main(int, const char * const * argv) -> int {
	self = *argv++;
	if(*argv && *argv == "--"sv)
		++argv;
	if(!*argv || (*(argv + 1) && *(argv + 2)))
		return std::fprintf(stderr, "usage: %s group [below-loadavg]\n", self.data()), 1;
	group = *argv++;

	struct timespec freq{5, 0};
	std::optional<double> load_target;
	if(*argv) {
		freq = {30, 0};  // matches loadavg_dam
		if(auto err = vore::parse_floating(*argv, load_target.emplace()); err || ((load_target <= 0) && (err = "<= 0")))
			return std::fprintf(stderr, "%s: %s: %s\n", self.data(), *argv, err), 1;
	}


	shm_t * shm;
	if(auto err = bundle::map(self.data(), shm))
		return err;

	if(auto err = sd_bus_default_system(&bus); err < 0)
		return std::fprintf(stderr, "%s: %s\n", self.data(), std::strerror(-err)), 1;

	const auto pid = getpid();
	std::string_view self_object;
	{
		sd_bus_message * self_object_m;  // we leak this to get it for static lifetime
		if(sd_bus_error err{}; sd_bus_call_method(bus, "org.freedesktop.systemd1", "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", "GetUnitByPID",
		                                          &err, &self_object_m, "u", static_cast<unsigned>(pid)) < 0)
			return std::fprintf(stderr, "%s: %s: %s\n", self.data(), err.message, err.name), 1;

		const char * self_object_s;
		if(auto err = sd_bus_message_read(self_object_m, "o", &self_object_s); err <= 0)
			return std::fprintf(stderr, "%s: %s\n", self.data(), std::strerror(-err)), 1;
		self_object = self_object_s;
	}

	for(;;) {
		if(load_target)
			if(double load; getloadavg(&load, 1) != 1 || load > *load_target) {
				nanosleep(&freq, nullptr);
				continue;  // load too high
			}

		const auto last = shm->load();
		clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
		if(!((now - last) > freq)) {
			// multiple jobs started at the same time may, due to jitter, spawn 20ms apart,
			// and the status change due to the first exiting races the second throttle_group,
			// which leads to apparent double-spend; stabilise for 5s (testing says 1s is too short)
			// (this means we can only spawn 0.2 throttled jobs per second. ah well)
			nanosleep(&freq, nullptr);
			continue;
		}


		sd_bus_message * units_r;
		if(sd_bus_error err{}; sd_bus_call_method(bus, "org.freedesktop.systemd1", "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager",
		                                          "ListUnitsByPatterns", &err, &units_r, "asas", 0u, 1u, "cron-*.service") < 0)
			return std::fprintf(stderr, "%s: %s: %s\n", self.data(), err.message, err.name), 1;
		std::unique_ptr<sd_bus_message, sd_bus_message_deleter> units{units_r};
		sd_bus_message_enter_container(units.get(), 'a', "(ssssssouso)");

		winner = {};
		std::forward_list<listed_unit> listed_units;
		for(;;) {
			const char *name, *description, *load_state, *active_state, *active_sub_state, *following, *object, *job_type, *job_path;
			uint32_t job_id;
			if(auto err = sd_bus_message_read(units.get(), "(ssssssouso)", &name, &description, &load_state, &active_state, &active_sub_state, &following, &object,
			                                  &job_id, &job_type, &job_path);
			   err <= 0 && err != -EBUSY) {
				if(err == 0)
					break;
				return std::fprintf(stderr, "%s: %s\n", self.data(), std::strerror(-err)), 1;
			}

			if(std::strchr(name, '@') || load_state != "loaded"sv)
				continue;
			if(object == self_object)
				continue;

			auto & unit           = listed_units.emplace_front();
			unit.object           = object;
			unit.active_state     = active_state;
			unit.active_sub_state = active_sub_state;
			sd_bus_slot * slot;
			if(sd_bus_get_property_async(
			       bus, &slot, "org.freedesktop.systemd1", object, "org.freedesktop.systemd1.Service", "ExecStartPre",
			       [](sd_bus_message * m, void * userdata, sd_bus_error *) { return static_cast<listed_unit *>(userdata)->ExecStartPre(m); }, &unit) < 0)
				continue;
			unit.slot.reset(slot);
		}

		for(std::uint64_t timeout; !winner;) {
			int err = sd_bus_process(bus, nullptr);
			if(err < 0 || (err = sd_bus_get_timeout(bus, &timeout)) < 0)
				return std::fprintf(stderr, "%s: %s\n", self.data(), std::strerror(-err)), 1;
			if(!err)  // sd_bus_get_timeout() returns 0 when no messages are pending
				break;
			if((err = sd_bus_wait(bus, timeout)) < 0)
				return std::fprintf(stderr, "%s: %s\n", self.data(), std::strerror(-err)), 1;
		}


		if(winner) {  // we decisively know how long to sleep for (because a unit from our group is running)
			            // listed_units going out of scope cancels pending request slots in case we short-circuited
			nanosleep(&winner->result, nullptr);
			continue;
		}
		if(auto sleep = std::accumulate(std::begin(listed_units), std::end(listed_units), (struct timespec){},
		                                [](auto && acc, auto && unit) { return std::max(unit.result, acc); });
		   sleep.tv_sec || sleep.tv_nsec) {  // a unit from our group exited less than BLANKING_INTERVAL ago, sleep for the remaining time
			nanosleep(&sleep, nullptr);
			continue;
		}

		if(auto value = last; !shm->compare_exchange_strong(value, {pid, now})) {
			nanosleep(&freq, nullptr);  // someone else did something in the mean-time, look again
			continue;
		}

		break;
	}
}
