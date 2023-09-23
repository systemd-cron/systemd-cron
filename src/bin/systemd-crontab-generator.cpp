#include "libvoreutils.hpp"
#include "util.hpp"
#include <openssl/evp.h>
static const constexpr auto key_or_plain = [](auto && lhs, auto && rhs) {
	static const constexpr auto key =
	    vore::overload{[](const std::string_view & s) { return s; }, [](const std::pair<std::string_view, std::string_view> & kv) { return kv.first; }};
	return key(lhs) < key(rhs);
};


static const constexpr std::uint8_t MINUTES_SET[]  = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                                                      20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
                                                      40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59};
static const constexpr std::uint8_t HOURS_SET[]    = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23};
static const constexpr std::uint8_t DAYS_SET[]     = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                                                      17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
static const constexpr std::string_view DOWS_SET[] = {"Sun"sv, "Mon"sv, "Tue"sv, "Wed"sv, "Thu"sv, "Fri"sv, "Sat"sv, "Sun"sv};
static const constexpr std::uint8_t MONTHS_SET[]   = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
static const char * const MINUTES_RANGE            = "[0, 59]";
static const char * const HOURS_RANGE              = "[0, 23]";
static const char * const DAYS_RANGE               = "[1, 31]";
static const char * const DOWS_RANGE               = "[mon, sun] or [1, 7]";
static const char * const MONTHS_RANGE             = "[jan, dec] or [1, 12]";

#include "configuration.hpp"

static const char * SELF;
static bool RUN_BY_SYSTEMD;
static const constexpr std::string_view VALID_CHARS = "-"
                                                      "0123456789"
                                                      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                                      "_"
                                                      "abcdefghijklmnopqrstuvwxyz"sv;  // keep sorted


static const constexpr std::pair<std::string_view, std::string_view> PART2TIMER[] = {
// kept sorted by Makefile
#include "part2timer.hpp"
    {"\xFF"sv, ""sv},  // we can't have an empty array (if no mapping set), so this sorts after everything
};

static const constexpr std::pair<std::string_view, std::string_view> CROND2TIMER[] = {
// kept sorted by Makefile
#include "crond2timer.hpp"
    {"\xFF"sv, ""sv},  // we can't have an empty array (if no mapping set), so this sorts after everything
};

static auto which(const std::string_view & exe, std::optional<std::string_view> paths = {}) -> std::optional<std::string> {
	if(!paths)
		paths = std::getenv("PATH") ?: "/usr/bin:/bin";
	for(auto path : vore::soft_tokenise{*paths, ":"sv}) {
		auto abspath = (std::string{path} += '/') += exe;
		if(!access(abspath.c_str(), X_OK))
			return abspath;
	}
	return {};
}

static const bool HAS_SENDMAIL = [] {
	if(auto sendmail = std::getenv("SENDMAIL"))
		if(!access(sendmail, X_OK))
			return true;
	return which("sendmail"sv) || which("sendmail"sv, "/usr/sbin:/usr/lib"sv);
}();
static std::string_view TARGET_DIR;
static std::string TIMERS_DIR;
static std::optional<std::uint64_t> UPTIME;

static auto systemd_bool(const std::string_view & string) -> bool {
	return !strncasecmp(string.data(), "1", string.size()) ||    //
	       !strncasecmp(string.data(), "yes", string.size()) ||  //
	       !strncasecmp(string.data(), "true", string.size());
}
static auto systemd_bool_false(const std::string_view & string) -> bool {
	return !strncasecmp(string.data(), "0", string.size()) ||   //
	       !strncasecmp(string.data(), "no", string.size()) ||  //
	       !strncasecmp(string.data(), "false", string.size());
}

enum class Log : std::uint8_t { EMERG, ALERT, CRIT, ERR, WARNING, NOTICE, INFO, DEBUG };
// there could be an IGNORE level for logging the .placeholder
// with a priority even lower than DEBUG only log to stdout
// but never to dmesg

static __attribute__((format(printf, 2, 3))) void log(Log level, const char * fmt, ...) {
	va_list args;
	va_start(args, fmt);

	auto into = stderr;
	vore::file::FILE<false> kmsg;
	if(RUN_BY_SYSTEMD && (kmsg = {"/dev/kmsg", "we"})) {
		std::fprintf(kmsg, "<%" PRIu8 ">%s[%d]", static_cast<std::uint8_t>(level), SELF, getpid());
		into = kmsg;
	} else
		std::fputs(SELF, into);
	std::fputs(": ", into);
	std::vfprintf(into, fmt, args);
	std::fputc('\n', into);
	va_end(args);
}
#define FORMAT_SV(sv) (int)(sv).size(), (sv).data()


static auto int_map(const std::string_view & str, bool & err) -> std::size_t;
static auto month_map(const std::string_view & month, bool & err) -> std::size_t;
static auto dow_map(const std::string_view & dow_full, bool & err) -> std::size_t;
template <class T, class V>
static auto parse_period(const std::string_view & value, const V & values, std::set<T> & into, std::size_t (*mapping)(const std::string_view &, bool &),
                         std::size_t base) -> std::optional<const char *>;
static auto environment_write(const std::map<std::string_view, std::string_view> & env, FILE * into) -> void;


enum class withuser_t : std::uint8_t {
	from_cmd0,      // system crontab
	from_basename,  // users' crontabs
	initial,        // --check/--translate
};

enum class cron_mail_success_t : std::uint8_t {
	never,     // no OnSuccess=
	always,    // plain OnSuccess=
	nonempty,  // OnSuccess=...\x20nonempty

	dflt = nonempty,
};

enum class cron_mail_format_t : bool {
	normal,      // systemctl status + journalctl
	nometadata,  // journalctl -o cat

	dflt = normal,
};


struct Job {
	std::string filename;
	std::string_view basename;
	std::string_view line;
	std::vector<std::string_view> parts;
	std::map<std::string_view, std::string_view> environment;
	std::string environment_PATH_storage;  // borrowed into environment on expansion
	std::string_view shell;
	std::size_t random_delay;
	std::string period;  // either period or timespec
	std::optional<std::string_view> timezone;
	std::set<std::uint8_t> timespec_minute;   // 0-60                           or TIMESPEC_ASTERISK
	std::set<std::uint8_t> timespec_hour;     // 0-24                           or TIMESPEC_ASTERISK
	std::set<std::uint8_t> timespec_dom;      // 0-31                           or TIMESPEC_ASTERISK
	std::set<std::string_view> timespec_dow;  // 0-7 (actually sun-mon-...-sun) or "*"
	std::set<std::uint8_t> timespec_month;    // 0-12                           or TIMESPEC_ASTERISK
	bool sunday_is_seven;
	std::string schedule;
	std::size_t boot_delay;
	std::size_t start_hour;
	bool persistent;
	bool batch;
	std::string jobid;
	std::string unit_name;
	std::string_view user;
	std::optional<std::string_view> home;  // 'static
	cron_mail_success_t cron_mail_success;
	cron_mail_format_t cron_mail_format;
	struct {
		vore::span<const std::string_view *> command;  // subview of parts
		std::optional<std::string> command0;           // except this is command[0] if set

		struct command_iter {
			using iterator_category = std::input_iterator_tag;
			using difference_type   = void;
			using value_type        = const std::string_view;
			using pointer           = const std::string_view *;
			using reference         = const std::string_view &;

			vore::span<const std::string_view *> command;
			std::optional<std::string_view> command0;

			command_iter & operator++() noexcept {
				if(this->command.size())
					this->command0 = *command.b++;
				else
					this->command0 = {};
				return *this;
			}

			constexpr bool operator==(const command_iter & rhs) const noexcept { return this->command0 == rhs.command0 && this->command.b == rhs.command.b; }
			constexpr bool operator!=(const command_iter & rhs) const noexcept { return !(*this == rhs); }

			constexpr const std::string_view & operator*() noexcept {
				if(!this->command0)
					++*this;
				return *this->command0;
			}
		};

		constexpr command_iter begin() const noexcept { return {this->command, this->command0}; }
		constexpr command_iter end() const noexcept { return {{this->command.e, this->command.e}, {}}; }
		constexpr std::size_t size() const noexcept { return this->command.size() + static_cast<bool>(this->command0); }
		constexpr std::string_view operator[](std::size_t i) const noexcept {
			if(this->command0) {
				if(!i)
					return *this->command0;
				--i;
			}
			return this->command[i];
		}
	} command;
	std::string execstart;
	bool valid;

	Job(std::string_view filename, std::string_view line) {
		this->filename = filename;
		this->basename = vore::basename(filename);
		this->line     = line;

		vore::soft_tokenise tokens{line, " \t\n"sv};
		std::copy(std::begin(tokens), std::end(tokens), std::back_inserter(this->parts));

		this->shell           = "/bin/sh"sv;
		this->boot_delay      = 0;
		this->start_hour      = 0;
		this->random_delay    = 0;
		this->persistent      = false;
		this->user            = "root"sv;
		this->command         = {};
		this->valid           = true;
		this->batch           = false;
		this->sunday_is_seven = false;

		this->cron_mail_success = cron_mail_success_t::dflt;
		this->cron_mail_format  = cron_mail_format_t::dflt;
	}

	auto log(Log priority, const char * message) -> void { ::log(priority, "%.*s: %.*s: %s", FORMAT_SV(this->filename), FORMAT_SV(this->line), message); }
	template <class... T>
#ifdef __clang__
	__attribute__((format(printf, 3, 4)))
#endif
	auto
	log(Log priority, const char * fmt, const T &... args) -> void {
		char buf[128];
		std::string_view msg{buf, static_cast<std::size_t>(std::snprintf(buf, sizeof(buf), fmt, args...))};
		::log(priority, "%.*s: %.*s: %.*s", FORMAT_SV(this->filename), FORMAT_SV(this->line), FORMAT_SV(msg));
	}

	auto which(const std::string_view & pgm) -> std::optional<std::string> {
		auto itr = this->environment.find("PATH"sv);
		return ::which(pgm, itr == std::end(this->environment) ? std::nullopt : std::optional{itr->second});
	}

	// decode some environment variables that influence the behaviour of systemd-cron itself
	auto decode_environment(const std::map<std::string_view, std::string_view> & environment, bool default_persistent,
	                        cron_mail_success_t default_cron_mail_success, cron_mail_format_t default_cron_mail_format) -> void {
		this->persistent        = default_persistent;
		this->cron_mail_success = default_cron_mail_success;
		this->cron_mail_format  = default_cron_mail_format;
		for(auto && [k, v] : environment) {
			if(k == "PERSISTENT"sv)
				this->persistent = systemd_bool(v);
			else if(k == "RANDOM_DELAY"sv) {
				bool err{};
				this->random_delay = int_map(v, err);
				if(err)
					this->log(Log::WARNING, "invalid RANDOM_DELAY");
			} else if(k == "START_HOURS_RANGE"sv) {
				bool err{};
				if(auto idx = v.find('-'); idx == std::string_view::npos)
					err = true;
				else
					this->start_hour = int_map(v.substr(0, idx), err);
				if(err)
					this->log(Log::WARNING, "invalid START_HOURS_RANGE");
			} else if(k == "DELAY"sv) {
				bool err{};
				this->boot_delay = int_map(v, err);
				if(err)
					this->log(Log::WARNING, "invalid DELAY");
			} else if(k == "BATCH"sv)
				this->batch = systemd_bool(v);
			else if(k == "CRON_MAIL_SUCCESS"sv) {
				if(v == "never"sv || systemd_bool_false(v))
					this->cron_mail_success = cron_mail_success_t::never;
				else if(v == "always"sv || systemd_bool(v))
					this->cron_mail_success = cron_mail_success_t::always;
				else if(v == "nonempty"sv || v == "non-empty"sv)
					this->cron_mail_success = cron_mail_success_t::nonempty;
				else if(v == "inherit"sv)
					this->cron_mail_success = default_cron_mail_success;
				else
					this->log(Log::WARNING, "unknown CRON_MAIL_SUCCESS value");
			} else if(k == "CRON_MAIL_FORMAT"sv) {
				if(v == "normal"sv)
					this->cron_mail_format = cron_mail_format_t::normal;
				else if(v == "nometadata"sv || v == "no-metadata"sv)
					this->cron_mail_format = cron_mail_format_t::nometadata;
				else if(v == "inherit"sv)
					this->cron_mail_format = default_cron_mail_format;
				else
					this->log(Log::WARNING, "unknown CRON_MAIL_FORMAT value");
			} else {
				if(k == "SHELL"sv)
					this->shell = v;

				if(k == "TZ"sv || k == "CRON_TZ"sv)
					if(!v.empty() && !access(("/usr/share/zoneinfo/"s += v).c_str(), F_OK))
						this->timezone = v;

				if(k == "MAILTO"sv && !v.empty())
					if(!HAS_SENDMAIL)
						this->log(Log::WARNING, "a MTA is not installed, but MAILTO is set");

				this->environment.emplace(k, v);
			}
		}
	}

	auto parse_anacrontab() -> void {
		if(this->parts.size() < 4) {
			this->valid = false;
			return;
		}

		this->period = this->parts[0];
		this->jobid  = "anacron-"s += this->parts[2];
		bool err{};
		this->boot_delay = int_map(this->parts[1], err);
		if(err)
			this->log(Log::WARNING, "invalid DELAY");
		this->command.command.b = &*(this->parts.begin() + 3);
		this->command.command.e = &*(this->parts.end());
	}

	// crontab --translate <something>
	auto parse_crontab_auto() -> void {
		if(this->line[0] == '@')
			this->parse_crontab_at(withuser_t::initial);
		else
			this->parse_crontab_timespec(withuser_t::initial);

		if(this->command.size()) {
			if(this->command.size() > 1 && getpwnam(MAYBE_DUPA(this->command[0]))) {
				this->user = this->command[0];
				++this->command.command.b;
			} else
				this->user = getpass_getlogin();

			this->decode_command();
			auto first = true;
			for(auto && hunk : this->command) {
				if(hunk.empty())
					continue;
				if(!std::exchange(first, false))
					this->execstart += ' ';
				this->execstart += hunk;
			}
		}
	}

	// @daily (user) do something
	auto parse_crontab_at(withuser_t withuser) -> void {
		if(this->parts.size() < (2 + (withuser == withuser_t::from_cmd0))) {
			this->valid = false;
			return;
		}

		this->period = this->parts[0];
		switch(withuser) {
			case withuser_t::from_cmd0:
				this->user              = this->parts[1];
				this->command.command.b = &*(this->parts.begin() + 2);
				break;
			case withuser_t::from_basename:
				this->user = this->basename;
				[[fallthrough]];
			case withuser_t::initial:
				this->command.command.b = &*(this->parts.begin() + 1);
				break;
		}
		this->command.command.e = &*(this->parts.end());
		this->jobid             = (std::string{this->basename} += '-') += this->user;
	}

	// 6 2 * * * (user) do something
	auto parse_crontab_timespec(withuser_t withuser) -> void {
		if(this->parts.size() < (6 + (withuser == withuser_t::from_cmd0))) {
			this->valid = false;
			return;
		}

		auto && minutes       = this->parts[0];
		auto && hours         = this->parts[1];
		auto && days          = this->parts[2];
		auto && months        = this->parts[3];
		auto && dows          = this->parts[4];
		this->timespec_minute = this->parse_time_unit<false, std::uint8_t>(minutes, "minute", MINUTES_SET, MINUTES_RANGE, int_map);
		this->timespec_hour   = this->parse_time_unit<false, std::uint8_t>(hours, "hour", HOURS_SET, HOURS_RANGE, int_map);
		this->timespec_dom    = this->parse_time_unit<false, std::uint8_t>(days, "day", DAYS_SET, DAYS_RANGE, int_map);
		this->timespec_dow    = this->parse_time_unit<true, std::string_view>(dows, "dow", DOWS_SET, DOWS_RANGE, dow_map);
		this->timespec_month  = this->parse_time_unit<false, std::uint8_t>(months, "month", MONTHS_SET, MONTHS_RANGE, month_map);

		this->sunday_is_seven = dows.back() == '7' || [&] {
			if(dows.size() < 3)
				return false;
			char buf[3]{};
			std::transform(std::end(dows) - 3, std::end(dows), buf, ::tolower);
			return std::string_view{buf, 3} == "sun"sv;
		}();

		switch(withuser) {
			case withuser_t::from_cmd0:
				this->user              = this->parts[5];
				this->command.command.b = &*(this->parts.begin() + 6);
				break;
			case withuser_t::from_basename:
				this->user = this->basename;
				[[fallthrough]];
			case withuser_t::initial:
				this->command.command.b = &*(this->parts.begin() + 5);
				break;
		}
		this->command.command.e = &*(this->parts.end());
		this->jobid             = (std::string{this->basename} += '-') += this->user;
	}

	static const constexpr std::uint8_t TIMESPEC_ASTERISK = -1;
	template <bool DOW, class T, class V>
	auto parse_time_unit(const std::string_view & value, const char * field, const V & values, const char * range,
	                     std::size_t (*mapping)(const std::string_view &, bool &)) -> std::set<T> {
		if(value == "*"sv) {
			if constexpr(DOW)
				return {"*"sv};
			else
				return {TIMESPEC_ASTERISK};
		}

		std::size_t base;
		if constexpr(DOW)
			base = 0;
		else
			base = *std::min_element(std::begin(values), std::end(values));

		std::set<T> result;
		for(auto && subval : vore::soft_tokenise{value, ","sv})  // NOTE: this glides over consecutive commas so "0,,3" is accepted as-if "0,3"
			if(auto err = parse_period(subval, values, result, mapping, base)) {
				if(*err)
					this->log(Log::ERR, "field %s=%.*s (%.*s): %s", field, FORMAT_SV(value), FORMAT_SV(subval), *err);
				else {
					if(auto slash = subval.find('/'); slash != std::string_view::npos)
						subval.remove_suffix(subval.size() - slash);
					this->log(Log::ERR, "field %s=%.*s (%.*s), may be * or %s", field, FORMAT_SV(value), FORMAT_SV(subval), range);
				}
				this->valid = false;
				return {};
			}
		return result;
	}

	// decode & validate
	auto decode() -> void {
		this->jobid.erase(std::remove_if(std::begin(this->jobid), std::end(this->jobid),
		                                 [](char c) { return !std::binary_search(std::begin(VALID_CHARS), std::end(VALID_CHARS), c); }),
		                  std::end(this->jobid));
		this->decode_command();
	}

	// perform smart substitutions for known shells
	auto decode_command() -> void {
		if(!this->command.size())
			return;


		if(!this->home) {
			static std::map<std::string, std::string, std::less<>> pwnam_cache;
			auto itr = pwnam_cache.find(this->user);
			if(itr == std::end(pwnam_cache)) {
				std::string user{this->user};
				if(auto ent = getpwnam(user.data()))
					itr = pwnam_cache.emplace(std::move(user), ent->pw_dir).first;
			}
			if(itr != std::end(pwnam_cache))
				this->home = itr->second;
		}

		if(this->home) {
			if(this->command[0].starts_with("~/"sv)) {
				this->command.command0 = std::string{ * this->home} += this->command[0].substr(1);
				++this->command.command.b;
			}

			if(auto itr = this->environment.find("PATH"sv); itr != std::end(this->environment))
				if(itr->second.starts_with("~/") || itr->second.find(":~/"sv) != std::string_view::npos) {
					for(auto && path : vore::soft_tokenise{itr->second, ":"sv}) {
						if(path.starts_with("~/")) {
							this->environment_PATH_storage += *this->home;
							this->environment_PATH_storage += path.substr(1);
						} else
							this->environment_PATH_storage += path;
						this->environment_PATH_storage += ':';
					}
					this->environment_PATH_storage.pop_back();
					this->environment["PATH"sv] = this->environment_PATH_storage;
				}
		}
	}

	auto is_active() -> bool {
		if(this->schedule == "reboot"sv && !access(REBOOT_FILE, F_OK))
			return false;

		return true;
	}

	auto generate_schedule_from_period() -> void {
		static const constexpr std::string_view TIME_UNITS_SET[] = {"daily"sv,         "monthly"sv, "quarterly"sv,
		                                                            "semi-annually"sv, "weekly"sv,  "yearly"sv};  // keep sorted

		if(auto i = this->period.find_first_not_of('@'); i != std::string::npos)
			this->period.erase(0, i);
		else
			this->period = {};
		std::transform(std::begin(this->period), std::end(this->period), std::begin(this->period), ::tolower);
		static const constexpr std::pair<std::string_view, std::string_view> replacements[] = {
		    // keep sorted
		    {"1"sv, "daily"sv},
		    {"30"sv, "monthly"sv},
		    {"31"sv, "monthly"sv},
		    {"365"sv, "yearly"sv},
		    {"7"sv, "weekly"sv},
		    {"annually"sv, "yearly"sv},
		    {"anually"sv, "yearly"sv},
		    {"bi-annually"sv, "semi-annually"sv},
		    {"biannually"sv, "semi-annually"sv},
		    {"boot"sv, "reboot"sv},
		    {"semiannually"sv, "semi-annually"sv},
		};
		if(auto itr = vore::binary_find(std::begin(replacements), std::end(replacements), this->period, key_or_plain); itr != std::end(replacements))
			this->period = itr->second;

		char buf[128];
		auto hour = this->start_hour;
		if(this->period == "reboot"sv) {
			this->boot_delay = std::max(this->boot_delay, static_cast<std::size_t>(1));
			this->schedule   = this->period;
			this->persistent = false;
		} else if(this->period == "minutely"sv) {
			this->schedule   = this->period;
			this->persistent = false;
		} else if(this->period == "hourly" && this->boot_delay == 0)
			this->schedule = "hourly"sv;
		else if(this->period == "hourly"sv) {
			this->schedule   = {buf, static_cast<std::size_t>(std::snprintf(buf, sizeof(buf), "*-*-* *:%zu:0", this->boot_delay))};
			this->boot_delay = 0;
		} else if(this->period == "midnight" && this->boot_delay == 0)
			this->schedule = "daily"sv;
		else if(this->period == "midnight")
			this->schedule = {buf, static_cast<std::size_t>(std::snprintf(buf, sizeof(buf), "*-*-* 0:%zu:0", this->boot_delay))};
		else if(std::binary_search(std::begin(TIME_UNITS_SET), std::end(TIME_UNITS_SET), this->period) && hour == 0 && this->boot_delay == 0)
			this->schedule = this->period;
		else if(this->period == "daily"sv)
			this->schedule = {buf, static_cast<std::size_t>(std::snprintf(buf, sizeof(buf), "*-*-* %zu:%zu:0", hour, this->boot_delay))};
		else if(this->period == "weekly"sv)
			this->schedule = {buf, static_cast<std::size_t>(std::snprintf(buf, sizeof(buf), "Mon *-*-* %zu:%zu:0", hour, this->boot_delay))};
		else if(this->period == "monthly"sv)
			this->schedule = {buf, static_cast<std::size_t>(std::snprintf(buf, sizeof(buf), "*-*-1 %zu:%zu:0", hour, this->boot_delay))};
		else if(this->period == "quarterly"sv)
			this->schedule = {buf, static_cast<std::size_t>(std::snprintf(buf, sizeof(buf), "*-1,4,7,10-1 %zu:%zu:0", hour, this->boot_delay))};
		else if(this->period == "semi-annually"sv)
			this->schedule = {buf, static_cast<std::size_t>(std::snprintf(buf, sizeof(buf), "*-1,7-1 %zu:%zu:0", hour, this->boot_delay))};
		else if(this->period == "yearly"sv)
			this->schedule = {buf, static_cast<std::size_t>(std::snprintf(buf, sizeof(buf), "*-1-1 %zu:%zu:0", hour, this->boot_delay))};
		else {
			bool err{};
			auto prd = int_map(this->period, err);
			if(err) {
				this->log(Log::ERR, "unknown schedule");
				this->schedule = this->period;
				return;
			}

			if(prd > 31) {
				// workaround for anacrontab
				std::size_t divisor = std::round(static_cast<float>(prd) / 30);
				this->schedule      = {buf, static_cast<std::size_t>(std::snprintf(buf, sizeof(buf), "*-1/%zu-1 %zu:%zu:0", divisor, hour, this->boot_delay))};
			} else
				this->schedule = {buf, static_cast<std::size_t>(std::snprintf(buf, sizeof(buf), "*-*-1/%zu %zu:%zu:0", prd, hour, this->boot_delay))};
		}
	}

	auto generate_schedule() -> void {
		if(!this->period.empty())
			this->generate_schedule_from_period();
		else
			this->generate_schedule_from_timespec();
	}

	auto generate_schedule_from_timespec() -> void {
		std::string dows;
		if(this->timespec_dow.size() != 1 || *std::begin(this->timespec_dow) != "*"sv) {  // != ['*']
			for(auto && dow : vore::span{std::begin(DOWS_SET) + this->sunday_is_seven, std::end(DOWS_SET) - !this->sunday_is_seven}) {
				if(this->timespec_dow.find(dow) == std::end(this->timespec_dow))
					continue;
				if(!dows.empty())
					dows += ',';
				dows += dow;
			}
			dows += ' ';
		}

		this->timespec_month.erase(0);
		this->timespec_dom.erase(0);

		if(this->timespec_month.empty() || this->timespec_dom.empty() || this->timespec_hour.empty() || this->timespec_minute.empty()) {
			this->valid = false;
			// errors already dumped by the parser
			return;
		}

		// %s*-%s-%s %s:%s:00
		this->schedule = std::move(dows);
		this->schedule += "*-"sv;
		auto timespec_comma = [&](auto && field) {
			char buf[3 + 1];  // 255
			auto first = true;
			for(auto f : field) {
				if(!std::exchange(first, false))
					this->schedule += ',';
				if(f == TIMESPEC_ASTERISK)
					this->schedule += '*';
				else
					this->schedule += std::string_view{buf, static_cast<std::size_t>(std::snprintf(buf, sizeof(buf), "%" PRIu8 "", f))};
			}
		};
		timespec_comma(this->timespec_month);
		this->schedule += '-';
		timespec_comma(this->timespec_dom);
		this->schedule += ' ';
		timespec_comma(this->timespec_hour);
		this->schedule += ':';
		timespec_comma(this->timespec_minute);
		this->schedule += ":00"sv;
	}

	auto generate_scriptlet() -> std::optional<std::string> {
		// ...only if needed
		assert(!this->unit_name.empty());
		if(this->command.size() == 1) {
			struct stat sb;
			if(!stat(MAYBE_DUPA(this->command[0]), &sb) && S_ISREG(sb.st_mode)) {
				this->execstart = this->command[0];
				return {};
			}
		}

		auto scriptlet  = ((std::string{TARGET_DIR} += '/') += this->unit_name) += ".sh"sv;
		this->execstart = (std::string{this->shell} += ' ') += scriptlet;
		return scriptlet;
	}

	auto generate_unit_header(FILE * into, const char * tp) -> void {
		std::fputs("[Unit]\n", into);
		std::fprintf(into, "Description=[%s] ", tp);
		if(this->line[0] != '/')
			std::fputc('\"', into);
		{
			auto desc = this->line;
			wchar_t c;
			mbstate_t st{};
			for(std::size_t conv; !desc.empty() && (conv = std::mbrtowc(&c, desc.data(), desc.size(), &st)); desc.remove_prefix(conv))
				switch(conv) {
					case static_cast<std::size_t>(-1):  // EILSEQ: reset, output byte as \xXX
						st   = {};
						conv = 1;
						std::fprintf(into, "\\x%02hhx", desc[0]);
						break;
					case static_cast<std::size_t>(-2):  // incomplete: truncate
						conv = desc.size();
						break;
					default:
						if(c == '%')  // % is special in systemd units, %% is literal
							std::fputs("%%", into);
						else
							std::fwrite(desc.data(), 1, conv, into);
						continue;
				}
		}
		if(this->line[0] != '/')
			std::fputc('\"', into);
		std::fputc('\n', into);

		std::fputs("Documentation=man:systemd-crontab-generator(8)\n", into);
		if(this->filename != "-"sv)
			std::fprintf(into, "SourcePath=%.*s\n", FORMAT_SV(this->filename));
	}

	auto format_on_failure(FILE * into, const char * on, bool nonempty = false) -> void {
		std::fprintf(into, "On%s=cron-mail@%%n:%s", on, on);
		if(nonempty)
			std::fputs(":nonempty", into);
		switch(this->cron_mail_format) {
			case cron_mail_format_t::normal:
				break;
			case cron_mail_format_t::nometadata:
				std::fputs(":nometadata", into);
				break;
		}
		std::fputs(".service\n", into);
	}

	auto generate_service(FILE * into) -> void {
		this->generate_unit_header(into, "Cron");
		if(auto itr = this->environment.find("MAILTO"sv); itr != std::end(this->environment) && itr->second.empty())
			;  // mails explicitely disabled
		else if(!HAS_SENDMAIL)
			;  // mails automaticaly disabled
		else {
			this->format_on_failure(into, "Failure");

			switch(this->cron_mail_success) {
				case cron_mail_success_t::never:
					break;
				case cron_mail_success_t::always:
					this->format_on_failure(into, "Success");
					break;
				case cron_mail_success_t::nonempty:
					this->format_on_failure(into, "Success", true);
					break;
			}
		}
		if(this->user != "root"sv || this->filename.find(STATEDIR) != std::string_view::npos) {
			std::fputs("Requires=systemd-user-sessions.service\n", into);
			if(this->home)
				std::fprintf(into, "RequiresMountsFor=%.*s\n", FORMAT_SV(*this->home));
		}
		std::fputc('\n', into);

		std::fputs("[Service]\n", into);
		std::fprintf(into, "User=%.*s\n", FORMAT_SV(this->user));
		std::fputs("Type=oneshot\n", into);
		std::fputs("IgnoreSIGPIPE=false\n", into);
		std::fputs("SyslogFacility=cron\n", into);
		std::fputs("KillMode=process\n", into);
		if(USE_LOGLEVELMAX != "no"sv)
			std::fprintf(into, "LogLevelMax=%.*s\n", FORMAT_SV(USE_LOGLEVELMAX));
		if(!this->schedule.empty() && this->boot_delay)
			if(!UPTIME || this->boot_delay > *UPTIME)
				std::fprintf(into, "ExecStartPre=-%.*s %zu\n", FORMAT_SV(BOOT_DELAY), this->boot_delay);
		std::fprintf(into, "ExecStart=%.*s\n", FORMAT_SV(this->execstart));
		if(this->environment.size()) {
			std::fputs("Environment=", into);
			environment_write(this->environment, into);
			std::fputc('\n', into);
		}
		if(this->batch) {
			std::fputs("CPUSchedulingPolicy=idle\n", into);
			std::fputs("IOSchedulingClass=idle\n", into);
		}
	}

	auto generate_timer(FILE * into) -> void {
		this->generate_unit_header(into, "Timer");
		std::fputs("PartOf=cron.target\n", into);
		std::fputc('\n', into);

		std::fputs("[Timer]\n", into);
		if(this->schedule == "reboot"sv)
			std::fprintf(into, "OnBootSec=%zum\n", this->boot_delay);
		else {
			std::fprintf(into, "OnCalendar=%.*s", FORMAT_SV(this->schedule));
			if(this->timezone)
				std::fprintf(into, " %.*s", FORMAT_SV(*this->timezone));
			std::fputc('\n', into);
		}
		if(this->random_delay > 1)
			std::fprintf(into, "RandomizedDelaySec=%zum\n", this->random_delay);
		if(this->persistent)
			std::fputs("Persistent=true\n", into);
	}

	auto generate_unit_name(std::uint64_t & seq) -> void {
		assert(!this->jobid.empty());
		this->unit_name = ("cron-"s += this->jobid) += '-';
		if(!this->persistent) {
		plain:
			char buf[20 + 1];  // 18446744073709551615
			this->unit_name += std::string_view{buf, static_cast<std::size_t>(std::snprintf(buf, sizeof(buf), "%" PRIu64 "", seq++))};
		} else {
#define TRY_CRYPTO(...) \
	if(!__VA_ARGS__)      \
	goto plain
			static EVP_MD_CTX * evp = [] {
				auto evp = EVP_MD_CTX_new();
				assert(evp);
				return evp;
			}();

			TRY_CRYPTO(EVP_DigestInit_ex(evp, EVP_md5(), nullptr));
			TRY_CRYPTO(EVP_DigestUpdate(evp, this->schedule.data(), this->schedule.size()));
			for(auto && hunk : this->command) {
				if(hunk.empty())
					continue;
				TRY_CRYPTO(EVP_DigestUpdate(evp, "", 1));  // NUL byte
				TRY_CRYPTO(EVP_DigestUpdate(evp, hunk.data(), hunk.size()));
			}

			std::uint8_t hash[128 / 8];
			TRY_CRYPTO(EVP_DigestFinal(evp, hash, nullptr));

			char buf[(sizeof(hash) * 2) + 1], *cur = buf;
			for(auto b : hash)
				cur += std::sprintf(cur, "%02" PRIx8 "", b);
			this->unit_name += std::string_view{buf, sizeof(buf) - 1};
		}
	}

	// write the result in TARGET_DIR
	auto output() -> bool {
		auto output_err = [&](std::string_view f, const char * op) {
			char buf[512];
			std::snprintf(buf, sizeof(buf), "%.*s: %s: %s", FORMAT_SV(f), op, std::strerror(errno));
			this->log(Log::ERR, buf);
		};
		assert(!this->unit_name.empty());

		if(auto scriptlet = this->generate_scriptlet()) {  // as a side-effect also changes this->execstart
			vore::file::FILE<false> f{scriptlet->c_str(), "we"};
			if(!f)
				return output_err(*scriptlet, "create"), false;
			auto first = true;
			for(auto && hunk : this->command) {
				if(hunk.empty())
					continue;
				if(!std::exchange(first, false))
					std::fputc(' ', f);
				std::fwrite(hunk.data(), 1, hunk.size(), f);
			}
			if(!first)
				std::fputc('\n', f);
			if(std::ferror(f) || std::fflush(f))
				return output_err(*scriptlet, "write"), false;
		}

		auto timer = ((std::string{TARGET_DIR} += '/') += this->unit_name) += ".timer"sv;
		{
			vore::file::FILE<false> t{timer.c_str(), "we"};
			if(!t)
				return output_err(timer, "create"), false;
			this->generate_timer(t);
			if(std::ferror(t) || std::fflush(t))
				return output_err(timer, "write"), false;
		}

		if(symlink(timer.c_str(), (((std::string{TIMERS_DIR} += '/') += this->unit_name) += ".timer"sv).c_str()) == -1 && errno != EEXIST)
			return output_err(timer, "link"), false;

		auto service = ((std::string{TARGET_DIR} += '/') += this->unit_name) += ".service"sv;
		{
			vore::file::FILE<false> s{service.c_str(), "we"};
			if(!s)
				return output_err(service, "create"), false;
			this->generate_service(s);
			if(std::ferror(s) || std::fflush(s))
				return output_err(service, "write"), false;
		}
		return true;
	}
};


template <class F>
static auto for_each_file(const char * dirname, F && cbk) -> void {
	if(vore::file::DIR dir{dirname}) {
		auto fd = dirfd(dir);
		struct stat sb;
		for(auto && ent : dir)
			if(ent.d_type == DT_REG || (!fstatat(fd, ent.d_name, &sb, 0) && S_ISREG(sb.st_mode)))
				cbk(ent.d_name);
	}
}

static auto environment_write(const std::map<std::string_view, std::string_view> & env, FILE * into) -> void {
	auto first = true;
	for(auto && [k, v] : env) {
		if(!std::exchange(first, false))
			std::fputc(' ', into);
		auto quote = v.find(' ') != std::string::npos;
		if(quote)
			std::fputc('"', into);
		std::fwrite(k.data(), 1, k.size(), into);
		std::fputc('=', into);
		std::fwrite(v.data(), 1, v.size(), into);
		if(quote)
			std::fputc('"', into);
	}
}


template <class F>
static auto parse_crontab(std::string_view filename, withuser_t withuser, bool anacrontab, cron_mail_success_t default_cron_mail_success,
                          cron_mail_format_t default_cron_mail_format, F && cbk) -> bool {
	vore::file::mapping map;
	{
		vore::file::fd<true> f{filename.data(), O_RDONLY | O_CLOEXEC};
		if(f == -1) {
			if(withuser == withuser_t::initial)  // Treat ENOENT as an error only for crontab -t and crontab -T, otherwise ignore
				return false;
			else
				return errno == ENOENT;
		}

		struct stat sb;
		fstat(f, &sb);
		if(!sb.st_size)
			return true;

		map = {nullptr, static_cast<std::size_t>(sb.st_size), PROT_READ, MAP_PRIVATE, f, 0};
		if(!map)
			return false;
	}

	std::map<std::string_view, std::string_view> environment;
	for(auto && line : vore::soft_tokenise{map, "\n"sv}) {
		while(!line.empty() && std::isspace(line[0]))
			line.remove_prefix(1);
		if(line.empty() || line[0] == '#')
			continue;
		while(!line.empty() && std::isspace(line.back()))
			line.remove_suffix(1);

		regmatch_t matches[3] = {{.rm_so = 0, .rm_eo = static_cast<regoff_t>(line.size())}};
		if(!regexec(&ENVVAR_RE, line.data(), sizeof(matches) / sizeof(*matches), matches, REG_STARTEND)) {
			auto key   = line.substr(matches[1].rm_so, matches[1].rm_eo - matches[1].rm_so);
			auto value = line.substr(matches[2].rm_so, matches[2].rm_eo - matches[2].rm_so);
			for(char tostrip : {'\'', '\"', ' '}) {
				while(!value.empty() && value[0] == tostrip)
					value.remove_prefix(1);
				while(!value.empty() && value.back() == tostrip)
					value.remove_suffix(1);
			}
			if(key == "PERSISTENT"sv && value == "auto"sv)
				environment.erase("PERSISTENT"sv);
			else
				environment[key] = value;
			continue;
		}

		Job j{filename, line};
		if(anacrontab) {
			j.decode_environment(environment, /*default_persistent=*/true, default_cron_mail_success, default_cron_mail_format);
			j.parse_anacrontab();
		} else if(line[0] == '@') {
			j.decode_environment(environment, /*default_persistent=*/true, default_cron_mail_success, default_cron_mail_format);
			j.parse_crontab_at(withuser);
		} else {
			j.decode_environment(environment, /*default_persistent=*/false, default_cron_mail_success, default_cron_mail_format);
			j.parse_crontab_timespec(withuser);
		}
		j.decode();
		j.generate_schedule();
		cbk(j);
	}
	return true;
}


static auto int_map(const std::string_view & str, bool & err) -> std::size_t {
	std::size_t ret = -1;
	if(!vore::parse_uint<10>(MAYBE_DUPA(str), ret))
		err = true;
	return ret;
}

static auto month_map(const std::string_view & month, bool & err) -> std::size_t {
	static const constexpr std::string_view months[] = {"jan"sv, "feb"sv, "mar"sv, "apr"sv, "may"sv, "jun"sv,
	                                                    "jul"sv, "aug"sv, "sep"sv, "oct"sv, "nov"sv, "dec"sv};
	if(auto ret = int_map(month, err); !err)
		return ret;
	else {
		auto mon = month.substr(0, 3);
		char buf[3];
		std::transform(std::begin(mon), std::end(mon), buf, ::tolower);
		if(auto itr = std::find(std::begin(months), std::end(months), std::string_view{buf, mon.size()}); itr != std::end(months))
			return (itr - std::begin(months)) + 1;
		else {
			err = true;
			return 0;
		}
	}
}

static auto dow_map(const std::string_view & dow_full, bool & err) -> std::size_t {
	static const constexpr std::string_view dows[] = {"sun"sv, "mon"sv, "tue"sv, "wed"sv, "thu"sv, "fri"sv, "sat"sv};
	auto dow                                       = dow_full.substr(0, 3);
	char buf[3];
	std::transform(std::begin(dow), std::end(dow), buf, ::tolower);
	if(auto itr = std::find(std::begin(dows), std::end(dows), std::string_view{buf, dow.size()}); itr != std::end(dows))
		return itr - std::begin(dows);
	else {
		return int_map(dow_full, err);
	}
}

template <class T, class V>
static auto parse_period(const std::string_view & value, const V & values, std::set<T> & into, std::size_t (*mapping)(const std::string_view &, bool &),
                         std::size_t base) -> std::optional<const char *> {
	std::string_view range = value;
	std::size_t step       = 1;
	bool err{};
	if(auto idx = value.find('/'); idx != std::string_view::npos) {
		range     = value.substr(0, idx);
		auto rest = value.substr(idx + 1);
		if(rest.find('/') != std::string_view::npos)
			return "doubled /";
		step = int_map(rest, err);
		if(err)
			return "/-skip not an integer";
	}

	if(range == "*"sv) {
		for(ssize_t i = 0; i < std::distance(std::begin(values), std::end(values)); i += step)
			into.emplace(values[i]);
		return {};
	}


	auto start = range, end = range;
	if(auto idx = range.find('-'); idx != std::string_view::npos) {
		start = range.substr(0, idx);
		end   = range.substr(idx + 1);
		if(end.find('-') != std::string_view::npos)
			return "doubled -";
	}


	auto i_start = mapping(start, err) - 1 + !base;
	auto i_end   = std::min(mapping(end, err) + !base, static_cast<std::size_t>(std::distance(std::begin(values), std::end(values))));
	bool any{};
	for(std::size_t i = i_start; i < i_end; i += step) {
		into.emplace(values[i]);
		any = true;
	}
	return any ? std::nullopt : std::optional{nullptr};
}

static auto generate_timer_unit(Job & job) -> void {
	static std::map<std::string, std::uint64_t> seqs;
	if(job.valid && job.is_active()) {
		job.generate_unit_name(seqs[job.jobid]);
		job.output();
	}
}

// schedule rerun of generators after /var is mounted
static auto workaround_var_not_mounted() -> bool {
	auto service = std::string{TARGET_DIR} += "/cron-after-var.service"sv;
	if(vore::file::FILE<false> f{service.c_str(), "we"}) {
		std::fputs("[Unit]\n"
		           "Description=Rerun systemd-crontab-generator because /var is a separate mount\n"
		           "Documentation=man:systemd.cron(7)\n"
		           "After=cron.target\n"
		           "ConditionDirectoryNotEmpty=" STATEDIR "\n"
		           "\n"
		           "[Service]\n"
		           "Type=oneshot\n"
		           "ExecStart=/bin/sh -c \"systemctl daemon-reload ; systemctl try-restart cron.target\"\n",
		           f);

		if(std::ferror(f) || std::fflush(f))
			return false;
	} else
		return false;

	auto MULTIUSER_DIR = std::string{TARGET_DIR} += "/multi-user.target.wants"sv;
	if(mkdir(MULTIUSER_DIR.c_str(), 0777) == -1 && errno != EEXIST)
		return false;

	if(symlink(service.c_str(), (MULTIUSER_DIR += "/cron-after-var.service"sv).c_str()) && errno != EEXIST)
		return false;
	return true;
}

// check if distribution also provide a native .timer
static auto is_masked(const char * path, std::string_view name, vore::span<const std::pair<std::string_view, std::string_view> *> distro_mapping) -> bool {
	auto unit_file = ("/___/systemd/system/"s += name) += ".timer";
	for(auto root : {"lib", "etc", "run"}) {
		std::memcpy(unit_file.data() + 1, root, 3);

		if(!access(unit_file.c_str(), F_OK)) {
			const char * reason = "native timer is present";
			struct stat sb;
			if(!stat(unit_file.c_str(), &sb) && sb.st_size == 0)
				reason = "it is masked";
			log(Log::NOTICE, "ignoring %s/%.*s because %s", path, FORMAT_SV(name), reason);
			return true;
		}
	}

	auto mapped_name = name;
	if(auto itr = vore::binary_find(std::begin(distro_mapping), std::end(distro_mapping), name, key_or_plain); itr != std::end(distro_mapping))
		mapped_name = itr->second;
	auto name_distro = std::string{mapped_name} += ".timer"sv;
	if(!access(("/lib/systemd/system/"s += name_distro).c_str(), F_OK)) {
		log(Log::NOTICE, "ignoring %s/%.*s because there is %.*s", path, FORMAT_SV(name), FORMAT_SV(name_distro));
		return true;
	}

	return false;
}

static auto is_backup(const char * path, const std::string_view & name) -> bool {
	if(name == ".placeholder"sv)
		return true;

	bool backup = name[0] == '.' || name.find('~') != std::string_view::npos || name.find(".dpkg-"sv) != std::string_view::npos || name == "0anacron"sv;
	if(backup)
		log(Log::DEBUG, "ignoring %s/%.*s", path, FORMAT_SV(name));
	return backup;
}

static auto realmain() -> int {
	if(!mkdirp(TIMERS_DIR)) {
		log(Log::ERR, "making %.*s: %s", FORMAT_SV(TIMERS_DIR), std::strerror(errno));
		return 1;
	}

	std::optional<std::string> fallback_mailto;
	std::map<std::string_view, std::uint8_t> distro_start_hour;
	cron_mail_success_t toplevel_cron_mail_success = cron_mail_success_t::dflt;
	cron_mail_format_t toplevel_cron_mail_format   = cron_mail_format_t::dflt;

	if(!parse_crontab("/etc/crontab", withuser_t::from_cmd0, /*anacrontab=*/false, cron_mail_success_t::dflt, cron_mail_format_t::dflt, [&](auto && job) {
		   if(auto itr = job.environment.find("MAILTO"sv); itr != std::end(job.environment))
			   fallback_mailto = itr->second;
		   if(!job.valid) {
			   log(Log::ERR, "truncated line in /etc/crontab: %.*s", FORMAT_SV(job.line));
			   return;
		   }

		   toplevel_cron_mail_success = job.cron_mail_success;
		   toplevel_cron_mail_format  = job.cron_mail_format;

		   // legacy boilerplate: ignore jobs that run /etc/cron.hourly, daily, weekly, monthly
		   //                     (but save the starting hour for daily, weekly, and monthly)
		   if(job.line.find("/etc/cron.hourly"sv) != std::string_view::npos)
			   return;
		   for(auto && disableable_period : {"daily"sv, "weekly"sv, "monthly"sv})
			   if(job.line.find("/etc/cron."s += disableable_period) != std::string_view::npos) {
				   if(auto hour = *job.timespec_hour.begin(); hour != Job::TIMESPEC_ASTERISK)
					   distro_start_hour[disableable_period] = hour;

				   return;
			   }

		   generate_timer_unit(job);
	   }))
		log(Log::ERR, "%s: %s", "/etc/crontab", std::strerror(errno));


	for_each_file("/etc/cron.d", [&](std::string_view basename) {
		if(is_masked("/etc/cron.d", basename, {std::begin(CROND2TIMER), std::end(CROND2TIMER)}))
			return;
		if(is_backup("/etc/cron.d", basename))
			return;
		auto filename = "/etc/cron.d/"s += basename;
		if(!parse_crontab(filename, withuser_t::from_cmd0, /*anacrontab=*/false, toplevel_cron_mail_success, toplevel_cron_mail_format, [&](auto && job) {
			   if(!job.valid) {
				   log(Log::ERR, "truncated line in %.*s: %.*s", FORMAT_SV(filename), FORMAT_SV(job.line));
				   return;
			   }
			   if(fallback_mailto && job.environment.find("MAILTO"sv) == std::end(job.environment))
				   job.environment.emplace("MAILTO"sv, *fallback_mailto);
			   generate_timer_unit(job);
		   }))
			log(Log::ERR, "%s: %s", filename.c_str(), std::strerror(errno));
	});

	if(!USE_RUNPARTS) {
		auto i = 0u;
		for(auto period : {"hourly"sv, "daily"sv, "weekly"sv, "monthly"sv, "yearly"sv}) {
			++i;
			auto directory = "/etc/cron."s += period;
			if(struct stat sb; stat(directory.c_str(), &sb) || !S_ISDIR(sb.st_mode))
				continue;
			for_each_file(directory.c_str(), [&](std::string_view basename) {
				if(is_masked(directory.c_str(), basename, {std::begin(PART2TIMER), std::end(PART2TIMER)}))
					return;
				if(is_backup(directory.c_str(), basename))
					return;
				auto filename            = (directory + '/') += basename;
				std::string_view command = filename;
				Job job{filename, filename};
				job.persistent        = true;
				job.period            = period;
				job.start_hour        = distro_start_hour[period];  // default 0
				job.boot_delay        = i * 5;
				job.command           = {{&command, &command + 1}, {}};
				job.jobid             = (std::string{period} += '-') += basename;
				job.cron_mail_success = toplevel_cron_mail_success;
				job.cron_mail_format  = toplevel_cron_mail_format;
				job.decode();  // ensure clean jobid
				job.generate_schedule();
				if(fallback_mailto && job.environment.find("MAILTO"sv) == std::end(job.environment))
					job.environment.emplace("MAILTO"sv, *fallback_mailto);
				job.unit_name = "cron-" + job.jobid;
				job.output();
			});
		}
	}

	if(!parse_crontab("/etc/anacrontab", withuser_t::from_basename, /*anacrontab=*/true, toplevel_cron_mail_success, toplevel_cron_mail_format, [&](auto && job) {
		   if(!job.valid) {
			   log(Log::ERR, "truncated line in /etc/anacrontab: %.*s", FORMAT_SV(job.line));
			   return;
		   }
		   generate_timer_unit(job);
	   }))
		log(Log::ERR, "%s: %s", "/etc/anacrontab", std::strerror(errno));

	if(struct stat sb; !stat(STATEDIR, &sb) && S_ISDIR(sb.st_mode)) {
		// /var is avaible
		for_each_file(STATEDIR, [&](std::string_view basename) {
			if(basename.find('.') != std::string_view::npos)
				return;

			auto filename = (std::string{STATEDIR} += '/') += basename;
			if(!parse_crontab(filename, withuser_t::from_basename, /*anacrontab=*/false, toplevel_cron_mail_success, toplevel_cron_mail_format,
			                  [&](auto && job) { generate_timer_unit(job); }))
				log(Log::ERR, "%s: %s", filename.c_str(), std::strerror(errno));
		});

		vore::file::fd<false>{REBOOT_FILE, O_WRONLY | O_CREAT | O_CLOEXEC, 0666};
	} else {
		if(!workaround_var_not_mounted())
			log(Log::WARNING, "%s: %s", "cron-after-var.service", std::strerror(errno));
	}

	return 0;
}


static auto check(const char * cron_file) -> int {
	bool err{};
	if(!parse_crontab(cron_file, withuser_t::initial, /*anacrontab=*/false, cron_mail_success_t::dflt, cron_mail_format_t::dflt, [&](auto && job) {
		   if(!job.valid) {
			   err = true;
			   job.log(Log::ERR, "truncated line");
		   } else if(!job.period.empty()) {
			   static const constexpr std::string_view valid_periods[] = {"annually"sv,      "bi-annually"sv,  "biannually"sv, "daily"sv,     "hourly"sv,
			                                                              "midnight"sv,      "minutely"sv,     "monthly"sv,    "quarterly"sv, "reboot"sv,
			                                                              "semi-annually"sv, "semiannually"sv, "weekly"sv,     "yearly"sv};  // keep sorted
			   if(!std::binary_search(std::begin(valid_periods), std::end(valid_periods), job.period)) {
				   err = true;
				   job.log(Log::ERR, "unknown schedule");
			   }
		   } else if(job.timespec_month.contains(0) || job.timespec_dom.contains(0)) {
			   err = true;
			   job.log(Log::ERR, "month and day can't be 0");
		   }
	   })) {
		err = true;
		log(Log::ERR, "%s: %s", cron_file, std::strerror(errno));
	}
	return err;
}


static auto translate(const char * line) -> int {
	Job job{"-"sv, line};
	job.parse_crontab_auto();
	job.decode();
	job.decode_command();
	job.generate_schedule();


	vore::file::FILE<false> timer{3, "w"};
	if(!timer)
		return log(Log::ERR, "%s", std::strerror(errno)), 3;
	job.generate_timer(timer);
	std::fputs("#Persistent=true\n", timer);


	vore::file::FILE<false> service{4, "w"};
	if(!service)
		return log(Log::ERR, "%s", std::strerror(errno)), 4;
	job.generate_service(service);

	return !job.valid;
}


int main(int argc, const char * const * argv) {
	std::setlocale(LC_ALL, "C.UTF-8");
	if(argc == 1) {
		std::fprintf(stderr, "usage: %s destination_folder\n", argv[0]);
		return 1;
	}

	const char * file{};
	bool file_check;

	SELF       = vore::basename(std::string_view{argv[0]}).data();
	TARGET_DIR = argv[1];
	if(TARGET_DIR == "--check"sv || TARGET_DIR == "--translate"sv) {
		file_check = TARGET_DIR == "--check"sv;
		TARGET_DIR = "/ENOENT"sv;
		file       = argv[2] ?: "-";
	}
	TIMERS_DIR = std::string{TARGET_DIR} += "/cron.target.wants"sv;


	if(file)
		return file_check ? check(file) : translate(file);


	RUN_BY_SYSTEMD = argc == 4;
	if(RUN_BY_SYSTEMD)
		if(vore::file::FILE<false> up{"/proc/uptime", "re"})
			if(std::fscanf(up, "%" SCNu64 "", &UPTIME.emplace()) != 1)
				UPTIME = {};

	return realmain();
}
