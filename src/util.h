#ifndef UTIL_H
#define UTIL_H

#include "common.h"

#include <cstdint>
#include <memory>
#include <random>
#include <ranges>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#include <sys/wait.h>
#include <thread>

inline std::string escape_cmd_arg(std::string const& arg) {
    if (arg.empty()) {
        return "''"; // Handle empty strings
    }

    bool needs_quoting = false;
    std::string quoted_arg;
    for (char c : arg) {
        if (std::isspace(c) || c == '\'' || c == '\\' || c == '"' || c == '$' ||
            c == '`' || c == ';' || c == '&' || c == '|' || c == '*' ||
            c == '?' || c == '[' || c == ']' || c == '(' || c == ')' ||
            c == '<' || c == '>' || c == '#' || c == '!') {
            needs_quoting = true;
        }

        if (c == '\'') {
            quoted_arg += "'\\''";
        } else {
            quoted_arg += c;
        }
    }

    if (needs_quoting) {
        quoted_arg = "'" + quoted_arg + "'";
    }

    return quoted_arg;
}

inline bool is_sub_path(fs::path const& path, fs::path const& base) {
    auto const mismatch =
        std::mismatch(path.begin(), path.end(), base.begin(), base.end());
    return mismatch.second == base.end();
}

inline bool is_executable(fs::path const& p) {
    if (!fs::exists(p) || !fs::is_regular_file(p)) {
        return false;
    }

    if (access(p.c_str(), X_OK) == 0) {
        return true;
    }

    return false;
}

inline std::string read_from_pipe(int pipe_fd) {
    std::string result;
    constexpr std::size_t buffer_size = 4096;
    char buffer[buffer_size];

    while (true) {
        ssize_t bytes_read = ::read(pipe_fd, buffer, buffer_size);
        if (bytes_read < 0) {
            // retry if interrupted by signal; otherwise throw.
            if (errno == EINTR) {
                continue;
            }

            throw make_system_error(
                errno, STR("Failed to read from pipe: " << pipe_fd));
        }
        if (bytes_read == 0) {
            // EOF
            break;
        }
        result.append(buffer, static_cast<std::size_t>(bytes_read));
    }

    return result;
}

inline std::optional<fs::path> get_process_cwd(pid_t pid) {
    if (pid <= 0) {
        throw std::invalid_argument("Invalid PID.");
    }

    std::string path = "/proc/" + std::to_string(pid) + "/cwd";
    char buffer[PATH_MAX];
    ssize_t len = readlink(path.c_str(), buffer, sizeof(buffer) - 1);
    if (len == -1) {
        return {};
    }
    buffer[len] = '\0';
    return fs::path(buffer);
}

inline std::optional<fs::path> resolve_fd_filename(pid_t pid, int fd) {
    fs::path path =
        fs::path("/proc") / std::to_string(pid) / "fd" / std::to_string(fd);

    char resolved_path[PATH_MAX];
    ssize_t len =
        readlink(path.c_str(), resolved_path, sizeof(resolved_path) - 1);
    if (len == -1) {
        return {};
    }

    resolved_path[len] = '\0'; // readlink does not null-terminate
    return {std::string(resolved_path)};
}

inline std::vector<std::string> string_split(std::string const& str, char delim) {
    std::vector<std::string> lines;
    std::istringstream iss(str);
    std::string line;

    while (std::getline(iss, line, delim)) {
        lines.push_back(line);
    }

    return lines;
}

inline std::variant<std::uintmax_t, std::error_code> file_size(fs::path const& path) {
    std::error_code ec;
    std::uintmax_t size = fs::file_size(path, ec);

    if (ec) {
        return ec;
    } else {
        return size;
    }
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

inline fs::path create_temp_file(std::string const& prefix,
                          std::string const& suffix) {
    static std::random_device rd;
    static std::mt19937_64 engine(rd());
    static std::uniform_int_distribution<unsigned long long> dist;

    fs::path temp_dir = fs::temp_directory_path();
    fs::path temp_file;

    while (true) {
        unsigned long long random_num = dist(engine);
        std::string filename = STR(prefix << random_num << suffix);
        temp_file = temp_dir / filename;

        if (!fs::exists(temp_file)) {
            break;
        }
    }

    return temp_file;
}

inline fs::path get_user_cache_dir() {
    // assume Linux/Unix
    char const* xdgCacheHome = std::getenv("XDG_CACHE_HOME");

    if (xdgCacheHome) {
        return {xdgCacheHome};
    }

    char const* home = std::getenv("HOME");
    if (home) {
        return fs::path(home) / ".cache";
    } else {
        throw std::runtime_error("Unable to get user HOME directory");
    }
}

inline std::string string_trim(std::string const& s) {
    auto start = s.begin();
    auto end = s.end();

    while (start != end && std::isspace(*start)) {
        ++start;
    }

    while (end > start && std::isspace(*(end - 1))) {
        --end;
    }

    if (start < end) {
        return {start, end};
    } else {
        return {};
    }
}

template <typename T, typename S>
inline void print_collection(std::ostream& os, T const& collection, S const& sep) {
    if (std::empty(collection)) {
        return;
    }

    auto it = std::begin(collection);
    auto end = std::end(collection);

    // print the first element without a separator
    os << *it++;
    for (; it != end; ++it) {
        os << sep << *it;
    }
}

template <typename T, typename S>
inline std::string mk_string(T const& collection, S const& sep) {
    std::ostringstream res;
    print_collection(res, collection, sep);
    return res.str();
}

// template <typename FileCollection>
// void create_tar_archive(fs::path const& archive, FileCollection const& files)
// {
//     FILE* temp_file = std::tmpfile();
//     if (!temp_file) {
//         throw std::runtime_error("Error creating temporary file.");
//     }
//
//     for (auto const& file : files) {
//         std::fprintf(temp_file, "%s\n", file.string().c_str());
//     }
//
//     std::fflush(temp_file);
//     std::vector<std::string> command = {"tar",
//                                         "--absolute-names",
//                                         "--preserve-permissions",
//                                         "-cvf",
//                                         archive.string(),
//                                         "--files-from",
//                                         "/dev/fd/" +
//                                             std::to_string(fileno(temp_file))};
//
//     auto [tar_out, exit_code] = execute_command(command, true);
//     if (exit_code != 0) {
//         std::fclose(temp_file);
//         std::string msg = STR("Error creating tar archive: "
//                               << archive.string() << ". tar exit code:  "
//                               << exit_code << "\nOutput:\n"
//                               << tar_out);
//         throw std::runtime_error(msg);
//     }
// }

template <typename Collection>
std::unique_ptr<typename Collection::value_type[]>
inline collection_to_c_array(Collection const& container) {
    using T = typename Collection::value_type;
    static_assert(std::ranges::sized_range<Collection>);

    size_t const size = std::ranges::size(container);
    if (size == 0) {
        return nullptr;
    }

    std::unique_ptr<T[]> xs(new T[size + 1]); // +1 for NULL terminator
    std::ranges::copy(container, xs.get());
    xs[size] = T{};

    return xs;
}

template <typename Collection>
    requires std::is_same_v<typename Collection::value_type, std::string>
inline std::unique_ptr<char* const[]>
collection_to_c_array(Collection const& container) {
    size_t const size = std::ranges::size(container);
    if (size == 0) {
        return nullptr;
    }

    std::unique_ptr<char*[]> xs(new char*[size + 1]); // +1 for NULL terminator

    for (size_t i = 0; i < size; ++i) {
        xs[i] = const_cast<char*>(container[i].c_str());
    }
    xs[size] = nullptr;

    return xs;
}

struct WaitForSignalResult {
    enum Status { Success, Timeout, Exit, Signal } status;
    std::optional<int> detail;
};

inline WaitForSignalResult wait_for_signal(pid_t pid, int sig,
                                    std::chrono::milliseconds timeout) {
    using clock = std::chrono::steady_clock;
    auto start_time = clock::now();

    while (true) {
        int status = 0;
        pid_t w = waitpid(pid, &status, WNOHANG);

        if (w < 0) {
            throw make_system_error(errno, "waitpid");
        }

        if (w == pid) {
            if (WIFSTOPPED(status) && WSTOPSIG(status) == sig) {
                return {WaitForSignalResult::Success, {}};
            }

            if (WIFEXITED(status)) {
                return {WaitForSignalResult::Exit, WEXITSTATUS(status)};
            }

            if (WIFSIGNALED(status)) {
                return {
                    WaitForSignalResult::Signal,
                    WTERMSIG(status),
                };
            }
        }

        if (clock::now() - start_time > timeout) {
            return {WaitForSignalResult::Timeout, {}};
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

struct Pipe {
    int read_fd;
    int write_fd;
};

inline Pipe create_pipe() {
    int fds[2];
    if (pipe(fds) < 0) {
        throw make_system_error(errno, "pipe");
    }
    return {fds[0], fds[1]};
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
std::optional<std::array<std::string, N>>
inline string_split_n(std::string const& str, std::string const& delim) {
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