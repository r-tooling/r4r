#ifndef UTIL_H
#define UTIL_H

#include "common.h"

#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <regex>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

inline std::string escape_cmd_arg(std::string const& arg,
                                  bool single_quote = true,
                                  bool force = false) {
    if (arg.empty() && !force) {
        return arg;
    }

    // Check if escaping is needed
    bool needsEscaping = force;
    if (!needsEscaping) {
        for (char c : arg) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\'' || c == '"' ||
                c == '\\' || c == '$' || c == '`' || c == '&' || c == '|' ||
                c == '>' || c == '<' || c == '*' || c == '?' || c == '(' ||
                c == ')' || c == '[' || c == ']' || c == ';' || c == '!' ||
                c == '#') {
                needsEscaping = true;
                break;
            }
        }
    }

    if (!needsEscaping) {
        return arg;
    }

    std::string escaped;
    if (single_quote) {
        // Use single quotes - everything is literal except single quotes
        // which must be closed, escaped, and reopened
        escaped = "'";
        for (char c : arg) {
            if (c == '\'') {
                escaped += "'\\''";
            } else {
                escaped += c;
            }
        }
        escaped += "'";
    } else {
        // Use double quotes - need to escape $, `, ", \ and !
        escaped = "\"";
        for (char c : arg) {
            if (c == '$' || c == '`' || c == '"' || c == '\\' || c == '!') {
                escaped += '\\';
            }
            escaped += c;
        }
        escaped += "\"";
    }

    return escaped;
}

inline std::vector<std::string> string_split(std::string const& str,
                                             char delim) {
    std::vector<std::string> lines;
    std::istringstream iss(str);
    std::string line;

    while (std::getline(iss, line, delim)) {
        lines.push_back(line);
    }

    return lines;
}

inline bool string_contains(std::string const& haystack,
                            std::string const& needle) {
    return haystack.find(needle) != std::string::npos;
}

inline std::string remove_ansi(std::string const& input) {
    // Regular expression to match ANSI escape codes.
    // This regex covers most common ANSI escape sequences:
    // - \x1B\[...m (SGR codes)
    // - \x1B\[...;...m (SGR codes with multiple parameters)
    // - \x1B\[...K (EL codes)
    // - \x1B\[...J (ED codes)
    // - \x1B\[...h/\x1B\[...l (SM/RM codes)
    // - \x1B[0-9]*A/B/C/D/E/F/G/H/J/K/S/T/f/n/s/u (Other control codes)
    // - \x1B\][^\x07]*\x07 (OSC codes)
    // - \x1B\(./\x1B\). (Character set codes)
    // - \x1B#./\x1B%./\x1B(./\x1B). (Designator codes)
    // It's important to use raw string literal to avoid escaping backslashes in
    // the regex
    std::regex ansi_regex(
        R"(\x1B\[[0-9;]*[mKJhhlABCDFGJSTfnsu]|\x1B\][^\x07]*\x07|\x1B\(.|\x1B\).|\x1B#.|x1B%.|x1B\(.|x1B\).)");

    // Replace all matches with an empty string.
    return std::regex_replace(input, ansi_regex, "");
}

inline fs::path get_user_cache_dir() {
    // assume Linux/Unix
    char const* xdg = std::getenv("XDG_CACHE_HOME");

    if (xdg != nullptr) {
        return {xdg};
    }

    char const* home = std::getenv("HOME");
    if (home != nullptr) {
        return fs::path(home) / ".cache";
    }
    throw std::runtime_error("Unable to get user HOME directory");
}

template <typename T, typename S>
inline void print_collection(std::ostream& os, T const& collection,
                             S const& sep) {
    if (std::empty(collection)) {
        return;
    }

    auto it = std::begin(collection);
    auto end = std::end(collection);

    os << *it++;
    for (; it != end; ++it) {
        os << sep << *it;
    }
}

template <typename T, typename S>
inline std::string string_join(T const& collection, S const& sep) {
    std::ostringstream res;
    print_collection(res, collection, sep);
    return res.str();
}

template <typename Collection>
    requires std::is_same_v<typename Collection::value_type, std::string>
inline std::vector<char*> collection_to_c_array(Collection const& container) {
    std::vector<char*> xs;

    size_t size = std::ranges::size(container);
    if (size == 0) {
        return xs;
    }

    xs.reserve(size + 1);

    for (auto& x : container) {
        xs.push_back(const_cast<char*>(x.c_str()));
    }

    xs.push_back(nullptr);

    return xs;
}

template <typename Duration>
inline std::string format_elapsed_time(Duration elapsed) {
    using namespace std::chrono;
    using namespace std::chrono_literals;

    constexpr int MS_PER_SEC = 1000;
    constexpr int MS_PER_MIN = 60 * MS_PER_SEC;
    constexpr int MS_PER_HOUR = 60 * MS_PER_MIN;

    auto total_ms = duration_cast<milliseconds>(elapsed).count();

    if (total_ms < MS_PER_SEC) {
        return STR(total_ms << "ms");
    }

    auto total_seconds = duration_cast<duration<double>>(elapsed).count();
    if (total_ms < MS_PER_MIN) {
        return STR(std::fixed << std::setprecision(1) << total_seconds << "s");
    }

    auto mins = total_ms / MS_PER_MIN;
    auto remaining_ms = total_ms % MS_PER_MIN;

    if (total_ms < MS_PER_HOUR) {
        auto secs = remaining_ms / MS_PER_SEC;
        auto deci_secs = (remaining_ms % MS_PER_SEC) / 100;
        return STR(std::setfill('0')
                   << mins << ":" << std::setw(2) << secs << "." << deci_secs);
    }

    auto hrs = total_ms / MS_PER_HOUR;
    mins = (total_ms % MS_PER_HOUR) / MS_PER_MIN;
    auto secs = (total_ms % MS_PER_MIN) / MS_PER_SEC;

    return STR(std::setfill('0') << hrs << ":" << std::setw(2) << mins << ":"
                                 << std::setw(2) << secs);
}

template <size_t N>
std::optional<std::array<std::string, N>> inline string_split_n(
    std::string const& str, std::string const& delim) {
    std::array<std::string, N> result{};
    size_t start = 0;
    size_t end = 0;
    size_t i = 0;

    while ((end = str.find(delim, start)) != std::string::npos) {
        if (i < N) {
            result[i++] = str.substr(start, end - start);
            start = end + delim.length();
        } else {
            return std::nullopt;
        }
    }

    if (i < N) {
        result[i++] = str.substr(start);
        start = str.size();
    }

    if (i != N || start != str.size()) {
        return std::nullopt;
    }

    return result;
}

inline bool string_iequals(std::string const& s1, std::string const& s2) {
    if (s1.size() != s2.size()) {
        return false;
    }
    return std::equal(s1.begin(), s1.end(), s2.begin(),
                      [](unsigned char c1, unsigned char c2) {
                          return std::tolower(c1) == std::tolower(c2);
                      });
}

inline std::string string_trim(std::string const& s) {
    auto start = s.begin();
    auto end = s.end();

    while (start != end && (std::isspace(*start) != 0)) {
        ++start;
    }

    while (end > start && (std::isspace(*(end - 1)) != 0)) {
        --end;
    }

    if (start < end) {
        return {start, end};
    }
    return {};
}

inline std::string string_unquote(std::string const& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

inline std::string string_tolowercase(std::string const& s) {
    // from: https://en.cppreference.com/w/cpp/string/byte/tolower
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return res;
}

inline std::unordered_map<std::string, std::string>
load_os_release_map(std::istream& input_stream) {
    std::unordered_map<std::string, std::string> result;
    std::string line;

    while (std::getline(input_stream, line)) {
        line = string_trim(line);

        if (line.empty() || line[0] == '#') {
            continue;
        }

        auto pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = string_trim(line.substr(0, pos));
            std::string value = string_trim(line.substr(pos + 1));

            // sanitize value
            value = string_unquote(value);
            value = string_trim(value);

            result[key] = value;
        }
    }

    return result;
}

/**
 * @brief Parses /etc/os-release or /usr/lib/os-release file into a key-value
 * map.
 *
 * Tries to read from /etc/os-release first, if not found, falls back to
 * /usr/lib/os-release.
 *
 * @return std::unordered_map<std::string, std::string> Map of parsed key-value
 * pairs or empty map if neither exists
 */
inline std::unordered_map<std::string, std::string> load_os_release_map() {
    std::ifstream file("/etc/os-release");
    if (!file.is_open()) {
        file.open("/usr/lib/os-release");
    }

    if (file.is_open()) {
        return load_os_release_map(file);
    }

    return {};
}

struct OsRelease {
    std::string distribution;
    std::string release;
};

inline std::optional<OsRelease> load_os_release() {
    auto map = load_os_release_map();

    if (auto it = map.find("ID"); it != map.end()) {
        OsRelease res;
        res.distribution = string_tolowercase(it->second);

        if (auto it = map.find("VERSION_ID"); it != map.end()) {
            res.release = string_tolowercase(it->second);
        }
        return res;
    }

    return {};
}

template <typename T>
std::optional<T> to_number(std::string_view const& s) {
    T num;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), num);
    if (ec == std::errc() && ptr == s.end()) {
        return num;
    }
    return {};
}

template <typename Func>
auto stopwatch(Func&& func) {
    using clock = std::chrono::steady_clock;
    using result_type = std::invoke_result_t<Func>;

    auto start = clock::now();
    if constexpr (std::is_void_v<result_type>) {
        // If the callable returns void:
        std::forward<Func>(func)();
        auto end = clock::now();
        return end - start;
    } else {
        // If the callable returns a non-void type:
        auto result = std::forward<Func>(func)();
        auto end = clock::now();
        return std::make_pair(std::move(result), end - start);
    }
}

#endif // UTIL_H
