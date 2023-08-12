// SPDX-License-Identifier: 0BSD
// Derived from voreutils headers
#ifndef LIBVOREUTILS_HPP
#define LIBVOREUTILS_HPP

#include <algorithm>
#include <alloca.h>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <pwd.h>
#include <regex.h>
#include <set>
#include <stdarg.h>
#include <string>
#include <string_view>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace std::literals;

namespace vore::file {
	namespace {
		template <class C = char>
		class mapping {
		public:
			constexpr mapping() noexcept {}

			mapping(void * addr, std::size_t length, int prot, int flags, int fd, off_t offset) noexcept {
				void * ret = mmap(addr, length, prot, flags, fd, offset);
				if(ret != MAP_FAILED) {
					static_assert(sizeof(C) == 1);
					this->map    = {static_cast<C *>(ret), length};
					this->opened = true;
				}
			}

			mapping(const mapping &) = delete;
			constexpr mapping(mapping && oth) noexcept : map(oth.map), opened(oth.opened) { oth.opened = false; }

			constexpr mapping & operator=(mapping && oth) noexcept {
				this->map    = oth.map;
				this->opened = oth.opened;
				oth.opened   = false;
				return *this;
			}

			~mapping() {
				if(this->opened)
					munmap(const_cast<C *>(this->map.data()), this->map.size());
			}

			constexpr operator bool() const noexcept { return !this->map.empty(); }
			constexpr operator std::basic_string_view<C>() const noexcept { return this->map; }
			constexpr const std::basic_string_view<C> & operator*() const noexcept { return this->map; }
			constexpr const std::basic_string_view<C> * operator->() const noexcept { return &this->map; }


		private:
			std::basic_string_view<C> map = {};
			bool opened                   = false;
		};
	}
}

namespace vore {
	namespace {
		template <class CharT, class Traits>
		constexpr std::basic_string_view<CharT, Traits> basename(std::basic_string_view<CharT, Traits> str) noexcept {
			if(size_t idx = str.rfind('/'); idx != std::basic_string_view<CharT, Traits>::npos)
				str.remove_prefix(idx + 1);

			return str;
		}
	}
}


namespace vore::file {
	namespace {
		template <bool allow_stdio>
		class fd {
			template <bool as>
			friend class FILE;

		public:
			constexpr fd() noexcept = default;
			fd(const char * path, int flags, mode_t mode = 0, int from = AT_FDCWD) noexcept {
				if constexpr(allow_stdio)
					if(path[0] == '-' && !path[1]) {  // path == "-"sv but saves a strlen() call on libstdc++
						switch(flags & O_ACCMODE) {
							case O_RDONLY:
								this->desc = 0;
								return;
							case O_WRONLY:
								this->desc = 1;
								return;
							default:
								errno = EINVAL;
								return;
						}
					}

				while((this->desc = openat(from, path, flags, mode)) == -1 && errno == EINTR)
					;
				this->opened = this->desc != -1;
			}
			fd(int desc) noexcept : desc(desc), opened(true) {}

			fd(const fd &) = delete;
			constexpr fd(fd && oth) noexcept { *this = std::move(oth); }

			constexpr fd & operator=(fd && oth) noexcept {
				this->swap(oth);
				return *this;
			}

			~fd() {
				if(this->opened)
					close(this->desc);
			}

			constexpr operator int() const noexcept { return this->desc; }

			int take() & noexcept {
				this->opened = false;
				return this->desc;
			}

			constexpr void swap(fd & oth) noexcept {
				std::swap(this->desc, oth.desc);
				std::swap(this->opened, oth.opened);
			}

		private:
			int desc = -1;

		public:
			bool opened = false;
		};

		template <bool allow_stdio>
		class FILE {
		public:
			static FILE tmpfile() noexcept {
				FILE ret;
				ret.stream = std::tmpfile();
				ret.opened = ret.stream;
				return ret;
			}

			constexpr FILE() noexcept = default;

			FILE(const char * path, const char * opts) noexcept {
				if constexpr(allow_stdio)
					if(path[0] == '-' && !path[1]) {  // path == "-"sv but saves a strlen() call on libstdc++
						if(opts[0] && opts[1] == '+') {
							errno = EINVAL;
							return;
						}
						switch(opts[0]) {
							case 'r':
								this->stream = stdin;
								return;
							case 'w':
							case 'a':
								this->stream = stdout;
								return;
							default:
								errno = EINVAL;
								return;
						}
					}

				this->stream = std::fopen(path, opts);
				this->opened = this->stream;
			}

			FILE(const FILE &) = delete;
			constexpr FILE(FILE && oth) noexcept { *this = std::move(oth); }

			FILE(int oth, const char * opts) noexcept : stream(oth != -1 ? fdopen(oth, opts) : nullptr), opened(this->stream) {}
			FILE(fd<false> && oth, const char * opts) noexcept : FILE(static_cast<int>(oth), opts) {
				if(this->stream)
					oth.opened = false;
			}

			constexpr FILE & operator=(FILE && oth) noexcept {
				this->swap(oth);
				return *this;
			}

			~FILE() {
				if(this->opened)
					std::fclose(this->stream);
			}

			constexpr operator ::FILE *() const noexcept { return this->stream; }

			constexpr void swap(FILE & oth) noexcept {
				std::swap(this->stream, oth.stream);
				std::swap(this->opened, oth.opened);
			}

		private:
			::FILE * stream = nullptr;

		public:
			bool opened = false;
		};

		template <bool = false>
		FILE(fd<false> &&, const char *) -> FILE<false>;
	}
}
namespace vore::file {
	namespace {
		struct DIR_iter {
			::DIR * stream{};
			struct dirent * entry{};


			DIR_iter & operator++() noexcept {
				if(this->stream)
					do
						this->entry = readdir(this->stream);
					while(this->entry &&
					      ((this->entry->d_name[0] == '.' && this->entry->d_name[1] == '\0') ||  // this->entry == "."sv || this->entry == ".."sv, but saves trips to libc
					       (this->entry->d_name[0] == '.' && this->entry->d_name[1] == '.' && this->entry->d_name[2] == '\0')));
				return *this;
			}

			DIR_iter operator++(int) noexcept {
				const auto ret = *this;
				++(*this);
				return ret;
			}

			constexpr bool operator==(const DIR_iter & rhs) const noexcept { return this->entry == rhs.entry; }
			constexpr bool operator!=(const DIR_iter & rhs) const noexcept { return !(*this == rhs); }

			constexpr const dirent & operator*() const noexcept { return *this->entry; }
		};


		class DIR {
		public:
			using iterator = DIR_iter;


			DIR(const char * path) noexcept {
				this->stream = opendir(path);
				this->opened = this->stream;
			}

			DIR(const DIR &) = delete;
			constexpr DIR(DIR && oth) noexcept : stream(oth.stream), opened(oth.opened) { oth.opened = false; }

			~DIR() {
				if(this->opened)
					closedir(this->stream);
			}

			constexpr operator ::DIR *() const noexcept { return this->stream; }


			iterator begin() const noexcept { return ++iterator{this->stream}; }
			constexpr iterator end() const noexcept { return {}; }

		private:
			::DIR * stream = nullptr;
			bool opened    = false;
		};
	}
}

namespace vore {
	namespace {
		struct soft_tokenise_iter {  // merge_seps = true
			using iterator_category = std::input_iterator_tag;
			using difference_type   = void;
			using value_type        = std::string_view;
			using pointer           = std::string_view *;
			using reference         = std::string_view &;

			std::string_view delim;
			std::string_view remaining;
			std::string_view token = {};


			soft_tokenise_iter & operator++() noexcept {
				auto next = this->remaining.find_first_not_of(this->delim);
				if(next != std::string_view::npos)
					this->remaining.remove_prefix(next);
				auto len = this->remaining.find_first_of(this->delim);
				if(len != std::string_view::npos) {
					this->token = {this->remaining.data(), len};
					this->remaining.remove_prefix(len);
				} else {
					this->token     = this->remaining;
					this->remaining = {};
				}
				return *this;
			}

			soft_tokenise_iter operator++(int) noexcept {
				const auto ret = *this;
				++(*this);
				return ret;
			}

			constexpr bool operator==(const soft_tokenise_iter & rhs) const noexcept { return this->token == rhs.token; }
			constexpr bool operator!=(const soft_tokenise_iter & rhs) const noexcept { return !(*this == rhs); }

			constexpr std::string_view operator*() const noexcept { return this->token; }
		};


		struct soft_tokenise {
			using iterator = soft_tokenise_iter;


			std::string_view str;
			std::string_view delim;


			iterator begin() noexcept { return ++iterator{this->delim, this->str}; }
			constexpr iterator end() const noexcept { return {}; }
		};
	}
}


#ifndef strndupa
#define strndupa(str, maxlen)                                      \
	__extension__({                                                  \
		auto _strdupa_str = str;                                       \
		auto len          = strnlen(_strdupa_str, maxlen);             \
		auto ret          = reinterpret_cast<char *>(alloca(len + 1)); \
		std::memcpy(ret, _strdupa_str, len);                           \
		ret[len] = '\0';                                               \
		ret;                                                           \
	})
#endif


#define MAYBE_DUPA(strv)                                                       \
	__extension__({                                                              \
		auto && _strv = strv;                                                      \
		_strv[_strv.size()] ? strndupa(_strv.data(), _strv.size()) : _strv.data(); \
	})


namespace vore {
	namespace {
		template <int base = 0, class T>
		bool parse_uint(const char * val, T & out) {
			if(val[0] == '\0')
				return errno = EINVAL, false;
			if(val[0] == '-')
				return errno = ERANGE, false;

			char * end{};
			errno    = 0;
			auto res = std::strtoull(val, &end, base);
			out      = res;
			if(errno)
				return false;
			if(res > std::numeric_limits<T>::max())
				return errno = ERANGE, false;
			if(*end != '\0')
				return errno = EINVAL, false;

			return true;
		}
	}
}

namespace vore {
	namespace {
		template <class T>
		struct span {
			T b, e;

			constexpr T begin() const noexcept { return this->b; }
			constexpr T end() const noexcept { return this->e; }
			constexpr std::size_t size() const noexcept { return this->e - this->b; }
			constexpr decltype(*(T{})) & operator[](std::size_t i) const noexcept { return *(this->b + i); }
		};


		template <class T>
		span(T, T) -> span<T>;
	}
}

namespace vore {
	namespace {
		template <class I, class T>
		I binary_find(I begin, I end, const T & val) {  // std::binary_search() but returns the iterator instead
			begin = std::lower_bound(begin, end, val);
			return (!(begin == end) && !(val < *begin)) ? begin : end;
		}
		template <class I, class T, class Compare>
		I binary_find(I begin, I end, const T & val, Compare comp) {
			begin = std::lower_bound(begin, end, val, comp);
			return (!(begin == end) && !comp(val, *begin)) ? begin : end;
		}
	}
}

namespace vore {
	namespace {
		template <class... Ts>
		struct overload : Ts... {
			using Ts::operator()...;
		};
		template <class... Ts>
		overload(Ts...) -> overload<Ts...>;
	}
}
#endif
