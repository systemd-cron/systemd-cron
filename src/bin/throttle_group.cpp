/* Copyright (C) 2025 наб (nabijaczleweli@nabijaczleweli.xyz)
 * SPDX-License-Identifier: 0BSD
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
 *                                                                        # activating + start-pre means running ExecStartPre=,
 *                                                                        # which we need to exclude to not deadlock against other throttle_groups
 *    active_state ∈ {"active", "reloading"})                  # reloading is a special type of active
 * | | sleep(BLANKING_INTERVAL)                                # a unit from our group is currently running
 * | ^ short-circuit back to top
 * |
 * | map(object.get-property(ExecMainStartTimestampMonotonic)) &
 * | map(object.get-property(ExecMainExitTimestampMonotonic))  &
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
		[ "${name%@*}"  = "$name"  ] || continue  # first name may have garbage on front (doesn't matter for this)
		busctl --system get-property org.freedesktop.systemd1 "$object" org.freedesktop.systemd1.Service ExecMainStartTimestampMonotonic ExecMainExitTimestampMonotonic ExecStartPre | paste -s &
	done | while read -r _ ExecMainStartTimestampMonotonic _ ExecMainExitTimestampMonotonic _ ExecStartPre; do
		[ "${ExecStartPre%"\"$0\" \"$group\""*}" != "$ExecStartPre" ] || continue
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

		auto ExecMainOneTimestampMonotonic(sd_bus_message * m, std::optional<struct timespec> listed_unit::* member) -> int {
			if(auto err = sd_bus_message_enter_container(m, 'v', "t"); err < 0)
				return err;

			std::uint64_t usec;
			if(auto err = sd_bus_message_read(m, "t", &usec); err < 0)
				return err;

			(this->*member).emplace();
			(this->*member)->tv_sec  = usec / 1'000'000;
			(this->*member)->tv_nsec = (usec - ((this->*member)->tv_sec * 1'000'000)) * 1'000;

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
	if(!*argv || *(argv + 1))
		return std::fprintf(stderr, "usage: %s group\n", self.data()), 1;
	group = *argv;


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
		const auto last = shm->load();
		clock_gettime(CLOCK_MONOTONIC_COARSE, &now);

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
			       [](sd_bus_message * m, void * userdata, sd_bus_error *) {
				       auto & unit = *static_cast<listed_unit *>(userdata);

				       if(auto err = sd_bus_message_enter_container(m, 'v', "a(sasbttttuii)"); err < 0)
					       return err;
				       if(auto err = sd_bus_message_enter_container(m, 'a', "(sasbttttuii)"); err < 0)
					       return err;

				       while(!winner && !unit.ExecMainStartTimestampMonotonic_slot && !unit.ExecMainExitTimestampMonotonic_slot) {
					       if(auto err = sd_bus_message_enter_container(m, 'r', "sasbttttuii"); err < 0)
						       return err;

					       const char * s;
					       if(auto err = sd_bus_message_read(m, "s", &s); err <= 0)
						       return err;

					       if(auto err = sd_bus_message_enter_container(m, 'a', "s"); err < 0)
						       return err;
					       if(const char * argv[2]; sd_bus_message_read(m, "s", &argv[0]) > 0 && sd_bus_message_read(m, "s", &argv[1]) > 0) {
						       if(argv[0] == self && argv[1] == group) {
							       if((unit.active_state == "activating"sv && unit.active_sub_state != "start-pre"sv) ||  //
							          unit.active_state == "active"sv || unit.active_state == "reloading"sv) {
								       unit.result = BLANKING_INTERVAL;
								       winner      = &unit;
							       } else {
								       sd_bus_slot *start{}, *exit{};
								       sd_bus_get_property_async(
								           bus, &start, "org.freedesktop.systemd1", unit.object, "org.freedesktop.systemd1.Service", "ExecMainStartTimestampMonotonic",
								           [](sd_bus_message * m, void * userdata, sd_bus_error *) {
									           return static_cast<listed_unit *>(userdata)->ExecMainOneTimestampMonotonic(m, &listed_unit::ExecMainStartTimestampMonotonic);
								           },
								           &unit);
								       sd_bus_get_property_async(
								           bus, &exit, "org.freedesktop.systemd1", unit.object, "org.freedesktop.systemd1.Service", "ExecMainExitTimestampMonotonic",
								           [](sd_bus_message * m, void * userdata, sd_bus_error *) {
									           return static_cast<listed_unit *>(userdata)->ExecMainOneTimestampMonotonic(m, &listed_unit::ExecMainExitTimestampMonotonic);
								           },
								           &unit);
								       unit.ExecMainStartTimestampMonotonic_slot.reset(start);
								       unit.ExecMainExitTimestampMonotonic_slot.reset(exit);
							       }
						       }

						       while(sd_bus_message_read(m, "s", &s) == -EBUSY)
							       ;
					       }
					       if(auto err = sd_bus_message_exit_container(m); err <= 0)
						       return err;

					       if(auto err = sd_bus_message_skip(m, "bttttuii"); err <= 0)
						       return err;
					       if(auto err = sd_bus_message_exit_container(m); err <= 0)
						       return err;
				       }

				       return 0;
			       },
			       &unit) < 0)
				continue;
			unit.slot.reset(slot);
		}

		for(int err; !winner && (err = sd_bus_process(bus, nullptr));) {
			if(err < 0 || (err = sd_bus_wait(bus, 1'000 /* 1ms */)) < 0)
				return std::fprintf(stderr, "%s: %s\n", self.data(), std::strerror(-err)), 1;
		}
		if(winner) {  // we decisively know how long to sleep for (because a unit from our group is running)
			            // listed_units going out of scope cancels pending request slots in case we short-circuited
			nanosleep(&winner->result, nullptr);
			continue;
		}
		if(auto sleep = std::accumulate(std::begin(listed_units), std::end(listed_units), (struct timespec){},
		                                [](auto && acc, auto && unit) { return unit.result > acc ? unit.result : acc; });
		   sleep.tv_sec || sleep.tv_nsec) {  // a unit from our group exited less than BLANKING_INTERVAL ago, sleep for the remaining time
			nanosleep(&sleep, nullptr);
			continue;
		}

		if(auto value = last; !shm->compare_exchange_strong(value, {pid, now})) {
			sleep(5);  // someone else did something in the mean-time, look again
			continue;
		}

		break;
	}
}
