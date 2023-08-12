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
