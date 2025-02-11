#ifndef COMMON_H
#define COMMON_H

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;
using namespace std::string_literals;
using namespace std::string_view_literals;

#include <iostream>
#include <sstream>

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage, bugprone-macro-parentheses)
#define STR(x) (((std::stringstream&)(std::stringstream() << x)).str())

inline std::system_error make_system_error(int error_code,
                                           std::string const& message) {
    return {error_code, std::system_category(), message};
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define UNIMPLEMENTED() ::stop("not implemented", __FILE__, __LINE__);

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define UNREACHABLE() ::stop("reached unreachable", __FILE__, __LINE__);

[[noreturn]] inline void stop(char const* msg, char const* file, int line) {
    throw std::runtime_error(STR(file << ":" << line << " : " << msg));
}

// bytes for U+00A0 (non-breakable space) in UTF-8
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define NBSP "\xC2\xA0"
static inline std::string const kDelimUtf8 = NBSP;

#endif // COMMON_H
