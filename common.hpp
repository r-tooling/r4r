#pragma once
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;
using namespace std::string_literals;
using namespace std::string_view_literals;

#include <iostream>
#include <sstream>

#define STR(x) (((std::stringstream&)(std::stringstream() << x)).str())

inline std::system_error make_system_error(int error_code,
                                           std::string const& message) {
    return std::system_error(error_code, std::system_category(), message);
}

#define UNIMPLEMENTED()                                                        \
    do {                                                                       \
        ::stop("not implemented", __FILE__, __LINE__);                         \
    } while (0)

#define UNREACHABLE()                                                          \
    do {                                                                       \
        ::stop("reached unreachable", __FILE__, __LINE__);                     \
    } while (0)

[[noreturn]] inline void stop(const char* msg, const char* file, int line) {
    throw std::runtime_error(STR(file << ":" << line << " : " << msg));
}
