#include "configuration.hpp"
#include "libvoreutils.hpp"
#include "util.hpp"
#include <getopt.h>
#include <sys/wait.h>


#define CRONTAB_DIR STATEDIR
static const char * self;

static const bool HAVE_SETGID = [] {
	struct stat sb;
	return geteuid() != 0 && !stat(SETGID_HELPER, &sb) && S_ISREG(sb.st_mode) && sb.st_uid == 0 && sb.st_gid != 0 && sb.st_mode & S_ISGID && sb.st_mode & S_IXGRP;
}();


template <class... A>
static auto exec(const char * prog, A... args) -> int {
	execl(prog, self, static_cast<const char *>(args)..., static_cast<const char *>(nullptr));

	auto exec_err = errno;
	std::fprintf(stderr, "%s: %s: %s\n", self, prog, std::strerror(exec_err));
	return exec_err == ENOENT ? 127 : 126;
}


static const std::string_view current_user = getpass_getlogin();


static __attribute__((format(printf, 1, 2))) auto confirm(const char * fmt, ...) -> bool {
	for(char buf[128];;) {
		va_list args;
		va_start(args, fmt);
		std::vfprintf(stderr, fmt, args);
		va_end(args);

		if(!std::fgets(buf, sizeof(buf), stdin))
			return false;
		if(int resp = rpmatch(buf); resp != -1)
			return resp;
	}
}

static auto copy_FILE(FILE * from, FILE * to) -> void {
	std::uint8_t buf[4096];
	for(ssize_t rd; !std::feof(from) && (rd = std::fread(buf, 1, sizeof(buf), from));)
		std::fwrite(buf, 1, rd, to);
}

static const char * const generator_path = std::getenv("SYSTEMD_CRON_GENERATOR") ?: "/usr/lib/systemd/system-generators/systemd-crontab-generator";

static auto run_generator(const char * op, const char * file_or_line, bool file_is_file) -> int {
	vore::file::FILE<false> copy;
	if(file_is_file)
		if(struct stat sb; file_or_line == "-"sv && (fstat(0, &sb) || !S_ISREG(sb.st_mode))) {
			if(!(copy = vore::file::FILE<false>::tmpfile()))
				return std::fprintf(stderr, "%s: %s\n", self, std::strerror(errno)), 1;
			copy_FILE(stdin, copy);
			std::fflush(copy);
			std::rewind(copy);
		}

	switch(pid_t child = vfork()) {
		case -1:
			return std::fprintf(stderr, "%s: couldn't create child: %s\n", self, std::strerror(errno)), 125;
		case 0:  // child
			if(copy)
				dup2(fileno(copy), 0);
			_exit(exec(generator_path, op, file_or_line));
		default: {  // parent
			int childret;
			while(waitpid(child, &childret, 0) == -1 && errno == EINTR)  // no other errors possible
				;
			return WIFSIGNALED(childret) ? 128 + WTERMSIG(childret) : WEXITSTATUS(childret);
		}
	}
}

static const bool want_colour = !*(std::getenv("NO_COLOR") ?: "") && (std::getenv("TERM") ?: ""sv).find("color"sv) != std::string_view::npos && isatty(1);
#define COLOUR_BLUE "\033[1;34m"
#define COLOUR_RESET "\033[0m"
static void blue(const char * line) {
	if(want_colour)
		std::fputs(COLOUR_BLUE, stdout);
	std::fputs(line, stdout);
	if(want_colour)
		std::fputs(COLOUR_RESET, stdout);
}

static auto translate(const char * line) -> int {
	close(3);
	auto timer = vore::file::FILE<false>::tmpfile();
	if(!timer)
		return std::fprintf(stderr, "%s: %s\n", self, std::strerror(errno)), 1;
	assert(fileno(timer) == 3);

	close(4);
	auto service = vore::file::FILE<false>::tmpfile();
	if(!service)
		return std::fprintf(stderr, "%s: %s\n", self, std::strerror(errno)), 1;
	assert(fileno(service) == 4);

	auto err = run_generator("--translate", line, false);
	if(err)
		return err;

	blue("# /etc/systemd/system/$unit.timer\n");
	std::rewind(timer);
	copy_FILE(timer, stdout);
	std::fputc('\n', stdout);

	blue("# /etc/systemd/system/$unit.service\n");
	std::rewind(service);
	copy_FILE(service, stdout);
	std::fflush(stdout);

	// optional analysis: don't run if /dev/fd/3 doesn't exist (<=> /proc not mounted on Linux) and ignore execlp() return if sd-analyze unavailable
	// oddly, missing /run/systemd is perfectly okay
	if(!access("/dev/fd/3", R_OK)) {
		std::rewind(timer);
		std::rewind(service);
		execlp("systemd-analyze", "systemd-analyze", "verify", "/dev/fd/3:input.timer", "/dev/fd/4:input.service", static_cast<const char *>(nullptr));
	}
	return 0;
}


static auto check(const char * file) -> bool {
	return run_generator("--check", file, true) == 0;
}

static auto test(const char * file) -> bool {
	auto rc = check(file);
	if(rc)
		std::puts("No syntax issues were found in the crontab file.");
	else
		std::puts("Invalid crontab file. Syntax issues were found.");
	return !rc;
}

static auto version() -> int {
	std::puts(VERSION);
	return 0;
}

// try to fix things up if running as root
static auto try_chmod(const char * cron_file = nullptr, const char * user = nullptr) -> void {
	struct stat sb;
	if(stat(SETGID_HELPER, &sb))
		return;

	if(!chown(CRONTAB_DIR, 0, sb.st_gid))
		(void)chmod(CRONTAB_DIR, 01730);  // rwx-wx--T

	if(cron_file && user)
		if(auto ent = getpwnam(user))
			if(!chown(cron_file, ent->pw_uid, sb.st_gid))
				(void)chmod(cron_file, 00600);  // rw-------
}

static auto colour_crontab(FILE * f) -> void {
	char * line_raw{};
	std::size_t linecap{};
	for(ssize_t len; (len = getline(&line_raw, &linecap, f)) != -1;) {
		std::string_view line{line_raw, static_cast<std::size_t>(len)};

		const char * colour{};
		if(line[0] == '#')
			colour = COLOUR_BLUE;
		// other matchers

		if(colour)
			std::fputs(colour, stdout);
		std::fwrite(line.data(), 1, line.size(), stdout);
		if(colour)
			std::fputs(COLOUR_RESET, stdout);
	}
}

static auto list(const char * cron_file, const char * user) -> int {
	if(vore::file::FILE<false> f{cron_file, "r"}) {
		if(!want_colour)
			copy_FILE(f, stdout);
		else
			colour_crontab(f);
		check(cron_file);
		try_chmod(cron_file, user);
		return 0;
	}
	auto err = errno;

	if(err == ENOENT)
		return std::fprintf(stderr, "no crontab for %s\n", user), ENOENT;

	if(user != current_user)
		return std::fprintf(stderr, "you can not display %s's crontab\n", user), 1;

	if(HAVE_SETGID) {
		if(!want_colour)
			return exec(SETGID_HELPER, "r");

		int pipe[2];
		pipe2(pipe, O_CLOEXEC);
		switch(pid_t child = vfork()) {
			case -1:
				return std::fprintf(stderr, "%s: couldn't create child: %s\n", self, std::strerror(errno)), 125;
			case 0:  // child
				dup2(pipe[1], 1);
				_exit(exec(SETGID_HELPER, "r"));
			default: {  // parent
				close(pipe[1]);
				vore::file::FILE<false> f{pipe[0], "r"};
				if(!f)
					return std::fprintf(stderr, "%s\n", std::strerror(err)), 1;
				colour_crontab(f);

				int childret;
				while(waitpid(child, &childret, 0) == -1 && errno == EINTR)  // no other errors possible
					;
				return WIFSIGNALED(childret) ? 128 + WTERMSIG(childret) : WEXITSTATUS(childret);
			}
		}
	}

	return std::fprintf(stderr, "%s\n", std::strerror(err)), 1;
}


static auto remove(const char * cron_file, const char * user, bool ask) -> int {
	try_chmod();
	if(ask && !confirm("Are you sure you want to delete %s? ", cron_file))
		return 0;

	if(!unlink(cron_file))
		return 0;
	auto err = errno;

	if(err == ENOENT || (err == EROFS && !access(cron_file, F_OK)))
		return std::fprintf(stderr, "no crontab for %s\n", user), 0;
	if(err == EROFS)
		return std::fprintf(stderr, "%s is on a read-only filesystem\n", cron_file), 1;

	if(user != current_user)
		return std::fprintf(stderr, "you can not delete %s's crontab\n", user), 1;

	if(HAVE_SETGID)
		return exec(SETGID_HELPER, "d");

	if(err == EACCES)
		if(!truncate(cron_file, 0))
			return std::fprintf(stderr, "couldn't remove %s, wiped it instead\n", cron_file), 0;

	return std::fprintf(stderr, "%s\n", std::strerror(err)), 1;
}


namespace {
	struct autodeleting_path {
		char buf[PATH_MAX];
		bool armed = true;

		~autodeleting_path() {
			if(this->armed)
				unlink(this->buf);
		}
		operator char *() noexcept { return this->buf; }
	};
}

static auto replace_crontab(const char * cron_file, const char * user, FILE * from) -> bool {
	autodeleting_path final_tmp_path{.buf = {}, .armed = false};
	std::snprintf(final_tmp_path, sizeof(final_tmp_path.buf), "" CRONTAB_DIR "/%s.XXXXXX", user);

	vore::file::FILE<false> final_tmp;
	int final_tmp_fd = mkostemp(final_tmp_path, O_CLOEXEC);
	if(final_tmp_fd == -1 || !(final_tmp = {final_tmp_fd, "w"}))
		return false;
	final_tmp_path.armed = true;

	copy_FILE(from, final_tmp);
	if(std::fflush(final_tmp))
		return false;
	if(rename(final_tmp_path, cron_file))
		return false;
	final_tmp_path.armed = false;

	try_chmod(cron_file, user);
	return true;
}

static auto edit(const char * cron_file, const char * user) -> int {
	autodeleting_path tmp_path;
	vore::file::FILE<false> tmp;
	{
		std::snprintf(tmp_path, sizeof(tmp_path.buf), "%s/crontab_XXXXXX", std::getenv("TMPDIR") ?: "/tmp");
		int tmp_fd = mkostemp(tmp_path, O_CLOEXEC);
		if(tmp_fd == -1 || !(tmp = {tmp_fd, "w+"}))
			return std::fprintf(stderr, "%s\n", std::strerror(errno)), 1;

		if(vore::file::FILE<false> crontab{cron_file, "re"})
			copy_FILE(crontab, tmp);
		else {
			int err = errno;

			if(err == ENOENT)
				std::fputs("# min hour dom month dow command\n", tmp);
			else if(user != current_user)
				return std::fprintf(stderr, "you can not edit %s's crontab\n", user), 1;
			else if(HAVE_SETGID)
				switch(pid_t child = vfork()) {
					case -1:
						return std::fprintf(stderr, "%s: couldn't create child: %s\n", self, std::strerror(errno)), 125;
					case 0:  // child
						dup2(tmp_fd, 1);
						_exit(exec(SETGID_HELPER, "r"));
					default: {  // parent
						int childret;
						while(waitpid(child, &childret, 0) == -1 && errno == EINTR)  // no other errors possible
							;
						childret = WIFSIGNALED(childret) ? 128 + WTERMSIG(childret) : WEXITSTATUS(childret);

						if(childret) {
							if(childret == 127 || childret == ENOENT)  // ENOENT || helper returned ENOENT
								std::fputs("# min hour dom month dow command\n", tmp);
							else {
								// helper will send error to stderr
								std::fprintf(stderr, "failed to read %s\n", cron_file);
								return childret;
							}
						}
					}
				}
			else
				return std::fprintf(stderr, "%s: %s: %s\n", self, cron_file, std::strerror(err)), 1;
		}
		std::fflush(tmp);
	}


	switch(pid_t child = vfork()) {
		case -1:
			return std::fprintf(stderr, "%s: couldn't create child: %s\n", self, std::strerror(errno)), 125;
		case 0:  // child
			_exit(exec("/bin/sh", "-c",
			           "[ -n \"${EDITOR}\" ] && exec ${EDITOR} \"$0\"\n"
			           "[ -n \"${VISUAL}\" ] && exec ${VISUAL} \"$0\"\n"
			           "for e in editor vim nano mcedit; do\n"
			           "	command -v \"$e\" > /dev/null && exec \"$e\" \"$0\"\n"
			           "done\n"
			           "echo No editor found >&2\n"
			           "exit 127\n",
			           tmp_path));
		default: {  // parent
			int childret;
			while(waitpid(child, &childret, 0) == -1 && errno == EINTR)  // no other errors possible
				;
			childret = WIFSIGNALED(childret) ? 128 + WTERMSIG(childret) : WEXITSTATUS(childret);

			if(childret) {
				if(childret != 127) {  // !ENOENT
					tmp_path.armed = false;
					std::fprintf(stderr, "edit aborted, your edit is kept here: %s\n", tmp_path.buf);
				}
				return childret;
			}
		}
	}
	tmp_path.armed = false;

	if(!check(tmp_path))
		return std::fprintf(stderr, "not replacing crontab, your edit is kept here: %s\n", tmp_path.buf), 1;


	if(!std::freopen(tmp_path, "re", tmp)) {  // reopen in case editor replaced the file, treat removal as cancel
		std::fputs("edit aborted\n", stderr);
		tmp_path.armed = true;
		return 0;
	}
	if(replace_crontab(cron_file, user, tmp)) {
		tmp_path.armed = true;
		return 0;
	}

	int err = errno;

	if(user != current_user)
		return std::fprintf(stderr, "you can not edit %s's crontab, your edit is kept here: %s\n", user, tmp_path.buf), 1;

	if(err == ENOSPC)
		return std::fprintf(stderr, "no space left in " CRONTAB_DIR ", your edit is kept here: %s\n", tmp_path.buf), 1;
	if(err == EROFS)
		return std::fprintf(stderr, "" CRONTAB_DIR " is on a read-only filesystem, your edit is kept here: %s\n", tmp_path.buf), 1;

	if(HAVE_SETGID) {
		std::rewind(tmp);
		switch(pid_t child = vfork()) {
			case -1:
				return std::fprintf(stderr, "%s: couldn't create child: %s\n", self, std::strerror(errno)), 125;
			case 0:  // child
				dup2(fileno(tmp), 0);
				_exit(exec(SETGID_HELPER, "w"));
			default: {  // parent
				int childret;
				while(waitpid(child, &childret, 0) == -1 && errno == EINTR)  // no other errors possible
					;
				childret = WIFSIGNALED(childret) ? 128 + WTERMSIG(childret) : WEXITSTATUS(childret);

				if(childret)
					return std::fprintf(stderr, "your edit is kept here: %s\n", tmp_path.buf), childret;
				else {
					tmp_path.armed = true;
					return 0;
				}
			}
		}
	}

	return std::fprintf(stderr, "unexpected error %s, your edit is kept here: %s\n", std::strerror(err), tmp_path.buf), 1;
}


static auto show() -> int {
	if(geteuid() != 0)
		return std::fputs("must be privileged to use -s\n", stderr), 1;

	for(auto && ent : vore::file::DIR{CRONTAB_DIR}) {
		if(getpwnam(ent.d_name))
			std::puts(ent.d_name);
		else
			std::fprintf(stderr, "WARNING: crontab found with no matching user: %s\n", ent.d_name);
	}
	return 0;
}


static auto replace(const char * cron_file, const char * user, const char * file) -> int {
	vore::file::FILE<true> from_file{file, "re"};
	if(!from_file)
		return std::fprintf(stderr, "%s: %s: %s\n", self, file, std::strerror(errno)), 1;
	if(!from_file.opened)
		if(struct stat sb; fstat(0, &sb) || !S_ISREG(sb.st_mode)) {
			auto copy = vore::file::FILE<true>::tmpfile();
			if(!copy)
				return std::fprintf(stderr, "%s: %s\n", self, std::strerror(errno)), 1;
			copy_FILE(stdin, copy);
			std::fflush(copy);
			dup2(fileno(copy), 0);
			std::rewind(from_file);
		}

	if(!check(file))
		return std::fputs("not replacing crontab\n", stderr), 1;

	std::rewind(from_file);
	if(replace_crontab(cron_file, user, from_file))
		return 0;
	int err = errno;

	if(user != current_user)
		return std::fprintf(stderr, "you can not replace %s's crontab\n", user), 1;

	if(errno == ENOSPC)
		return std::fputs("no space left in " CRONTAB_DIR "\n", stderr), 1;
	if(errno == EROFS)
		return std::fputs("" CRONTAB_DIR "is on a read-only filesystem\n", stderr), 1;

	if(HAVE_SETGID) {
		std::rewind(from_file);
		switch(pid_t child = vfork()) {
			case -1:
				return std::fprintf(stderr, "%s: couldn't create child: %s\n", self, std::strerror(errno)), 125;
			case 0:  // child
				if(from_file.opened)
					dup2(fileno(from_file), 0);
				_exit(exec(SETGID_HELPER, "w"));
			default: {  // parent
				int childret;
				while(waitpid(child, &childret, 0) == -1 && errno == EINTR)  // no other errors possible
					;
				return WIFSIGNALED(childret) ? 128 + WTERMSIG(childret) : WEXITSTATUS(childret);
			}
		}
	}

	return std::fprintf(stderr, "%s: %s: %s\n", self, file, std::strerror(err)), 1;
}


enum class action_t { replace, list = 'l', remove = 'r', edit = 'e', show = 's', translate = 't', test = 'T', version = 'V' };

#define USAGE                                                                               \
	"usage:\n"                                                                                \
	"  %1$s -h                     Show this help message and exit.\n"                        \
	"  %1$s -s                     Show all user who have a crontab. (only for root)\n"       \
	"  %1$s [-u USER] [FILE]       Replace the current crontab, read it from STDIN or FILE\n" \
	"  %1$s -e [-u USER]           Open the current crontab with an editor\n"                 \
	"  %1$s -r [-u USER] [-i]      Remove a crontab, (-i: with confirmation)\n"               \
	"  %1$s -l [-u USER]           List current crontab.\n"                                   \
	"  %1$s -t line                Translate one crontab line.\n"                             \
	"  %1$s -T FILE                Check one whole crontab file.\n"                           \
	"  %1$s -V                     Display systemd-cron version.\n"                           \
	"\n"                                                                                      \
	"long options:\n"                                                                         \
	"  %1$s -e, --edit\n"                                                                     \
	"  %1$s -l, --list\n"                                                                     \
	"  %1$s -r, --remove\n"                                                                   \
	"  %1$s -s, --show\n"                                                                     \
	"  %1$s -T, --test\n"                                                                     \
	"  %1$s -t, --translate\n"                                                                \
	"  %1$s -V, --version\n"                                                                  \
	"  %1$s -u, --user\n"

static const constexpr struct option longopts[] = {{"list", no_argument, nullptr, 'l'},        //
                                                   {"remove", no_argument, nullptr, 'r'},      //
                                                   {"ask", no_argument, nullptr, 'i'},         //
                                                   {"edit", no_argument, nullptr, 'e'},        //
                                                   {"show", no_argument, nullptr, 's'},        //
                                                   {"translate", no_argument, nullptr, 't'},   //
                                                   {"test", no_argument, nullptr, 'T'},        //
                                                   {"version", no_argument, nullptr, 'V'},     //
                                                   {"help", no_argument, nullptr, 'h'},        //
                                                   {"user", required_argument, nullptr, 'u'},  //
                                                   {}};                                        //

auto main(int argc, char * const * argv) -> int {
	setlocale(LC_ALL, "");
	self = argv[0];
	bool ask{};
	auto action = action_t::replace;
	const char * user{};
	for(int arg; (arg = getopt_long(argc, argv, "lriestTVhu:", longopts, nullptr)) != -1;)
		switch(arg) {
			case 'l':
			case 'r':
			case 'e':
			case 's':
			case 't':
			case 'T':
			case 'V':
				action = static_cast<action_t>(arg);
				break;
			case 'i':
				ask = true;
				break;

			case 'u':
				user = optarg;
				break;
			case 'h':
			default:
				return std::fprintf(stderr, USAGE, self), 1;
		}
	if(argv[optind] && argv[optind + 1])
		return std::fprintf(stderr, USAGE, self), 1;


	const char * file{};
	if(argv[optind])
		switch(action) {
			case action_t::replace:
			case action_t::test:
			case action_t::translate:
				file = argv[optind];
				break;
			default:
				return std::fprintf(stderr, USAGE, self), 1;
		}
	else if(action == action_t::translate || action == action_t::test)
		return std::fprintf(stderr, USAGE, self), 1;
	if(!user)
		user = current_user.data();

	if(!user || !getpwnam(user))
		return std::fprintf(stderr, "user '%s' unknown\n", user), 1;


	// try to fixup CRONTAB_DIR if it has not been handled in package script
	if(access(CRONTAB_DIR, F_OK))
		if(!mkdirp(CRONTAB_DIR))
			return std::fprintf(stderr, "" CRONTAB_DIR " doesn't exist!\n"), 1;


	vore::file::fd<false>{REBOOT_FILE, O_WRONLY | O_CREAT | O_CLOEXEC, 0666};

	char cron_file[sizeof(CRONTAB_DIR "/") + LOGIN_NAME_MAX];
	std::snprintf(cron_file, sizeof(cron_file), CRONTAB_DIR "/%s", user);
	switch(action) {
		case action_t::replace:
			return replace(cron_file, user, file ?: "-");
		case action_t::list:
			return list(cron_file, user);
		case action_t::remove:
			return remove(cron_file, user, ask);
		case action_t::edit:
			return edit(cron_file, user);
		case action_t::show:
			return show();
		case action_t::test:
			return test(file);
		case action_t::translate:
			return translate(file);
		case action_t::version:
			return version();
		default:
			__builtin_unreachable();
	}
}
