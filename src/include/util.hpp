#include <string_view>
#include "libvoreutils.hpp"


static auto mkdirp(const std::string_view & path) -> bool {
	for(auto && seg : vore::soft_tokenise{path, "/"}) {
		std::string_view up_to_now{std::begin(path), std::end(seg)};
		if(mkdir(MAYBE_DUPA(up_to_now), 0777) == -1 && errno != EEXIST)
			return false;
	}
	return true;
}


// Matches https://github.com/python/cpython/blob/3.11/Lib/getpass.py
static auto getpass_getlogin() -> std::string_view {
	for(auto var : {"LOGNAME", "USER", "LNAME", "USERNAME"})
		if(auto val = std::getenv(var))
			return val;

	static char pwusername[LOGIN_NAME_MAX + 1];
	if(auto ent = getpwuid(getuid()))
		return std::strncpy(pwusername, ent->pw_name, LOGIN_NAME_MAX);

	return {};
}


static const regex_t ENVVAR_RE = [] {
	regex_t ret;
	assert(!regcomp(&ret, R"regex(^([A-Za-z_0-9]+)[[:space:]]*=[[:space:]]*(.*)$)regex", REG_EXTENDED | REG_NEWLINE));
	assert(ret.re_nsub == 2);
	return ret;
}();
