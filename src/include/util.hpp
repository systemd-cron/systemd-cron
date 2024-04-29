#include "libvoreutils.hpp"
#include <string_view>


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


// https://git.sr.ht/~nabijaczleweli/fzifdso/tree/a05110a75aa8ab1abf92a0193b5772b74036b80a/item/src/fido2.cpp#L220
struct b64 {
	static const constexpr char alphabet[]            = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	static const constexpr std::uint8_t alphabet_bits = 6 /*__builtin_ctz(alphabet.size())*/;

	FILE * into;
	std::uint16_t acc{};
	std::uint8_t accsz{};

	auto feed(const std::string_view & data) -> void {
		// fprintf(stderr, "feed='%.*s'\n", (int)data.size(), data.data());
		this->feed(reinterpret_cast<const std::uint8_t *>(data.data()), reinterpret_cast<const std::uint8_t *>(data.data() + data.size()));
	}
	auto feed(const std::uint8_t * beg, const std::uint8_t * end) -> void {
		for(;;) {
			if(this->accsz < alphabet_bits) {
				if(beg == end)
					break;
				this->acc |= static_cast<std::uint16_t>(*beg++) << ((16 - 8) - this->accsz);
				this->accsz += 8;
			}

			std::fputc(alphabet[this->acc >> (16 - alphabet_bits)], this->into);
			this->acc <<= alphabet_bits;
			this->accsz -= alphabet_bits;
		}
	}

	auto finish() -> void {
		if(this->accsz)
			std::fputc(alphabet[this->acc >> (16 - alphabet_bits)], this->into);

		if(this->accsz) {
			if(this->accsz <= 4)
				std::fputc('=', this->into);
			if(this->accsz == 2)
				std::fputc('=', this->into);
		}
	}
};
