#ifndef UTIL_H
#define UTIL_H

#include "common.h"

#include <fstream>
#include <ranges>
#include <regex>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

inline std::string escape_cmd_arg(std::string const& arg,
                                  bool single_quote = true,
                                  bool force = false) {
    std::string quoted = single_quote ? "\\\'" : "\\\"";
    char quoted_chr = single_quote ? '\'' : '"';

    if (arg.empty()) {
        return std::string(2, quoted_chr);
    }

    bool needs_quoting = false;
    std::string quoted_arg;
    for (char c : arg) {
        if ((std::isspace(c) != 0) || c == '\'' || c == '\\' || c == '"' ||
            c == '$' || c == '`' || c == ';' || c == '&' || c == '|' ||
            c == '*' || c == '?' || c == '[' || c == ']' || c == '(' ||
            c == ')' || c == '<' || c == '>' || c == '#' || c == '!') {
            needs_quoting = true;
        }

        if (c == quoted_chr) {
            quoted_arg += quoted;
        } else {
            quoted_arg += c;
        }
    }

    if (needs_quoting || force) {
        quoted_arg = quoted_chr + quoted_arg + quoted_chr;
    }

    return quoted_arg;
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

#endif // UTIL_H
