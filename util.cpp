#include "util.hpp"
#include "common.hpp"
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <grp.h>
#include <iostream>
#include <pwd.h>
#include <stdexcept>
#include <string>
#include <vector>

#include <array>
#include <optional>
#include <sys/wait.h>
#include <thread>

namespace util {

std::string escape_cmd_arg(std::string const& arg) {
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

bool is_sub_path(fs::path const& path, fs::path const& base) {
    auto const mismatch =
        std::mismatch(path.begin(), path.end(), base.begin(), base.end());
    return mismatch.second == base.end();
}

std::string execute_command(std::string const& command) {
    std::array<char, 128> buffer{};
    std::string result;

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to run command: " + command);
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }

    if (pclose(pipe) != 0) {
        throw std::runtime_error("Command execution failed: " + command);
    }

    return result;
}

bool is_executable(fs::path const& p) {
    if (!fs::exists(p) || !fs::is_regular_file(p)) {
        return false;
    }

    if (access(p.c_str(), X_OK) == 0) {
        return true;
    }

    return false;
}

std::string read_from_pipe(int pipe_fd) {
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

WaitForSignalResult wait_for_signal(pid_t pid, int sig,
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

Pipe create_pipe() {
    int fds[2];
    if (pipe(fds) < 0) {
        throw make_system_error(errno, "pipe");
    }
    return {fds[0], fds[1]};
}

std::optional<fs::path> get_process_cwd(pid_t pid) {
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

std::optional<fs::path> resolve_fd_filename(pid_t pid, int fd) {
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

std::vector<std::string> string_split(std::string const& str, char delim) {
    std::vector<std::string> lines;
    std::istringstream iss(str);
    std::string line;

    while (std::getline(iss, line, delim)) {
        lines.push_back(line);
    }

    return lines;
}

std::variant<std::uintmax_t, std::error_code> file_size(fs::path const& path) {
    std::error_code ec;
    std::uintmax_t size = fs::file_size(path, ec);

    if (ec) {
        return ec;
    } else {
        return size;
    }
}

std::string remove_ansi(std::string const& input) {
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
fs::path create_temp_file(std::string const& prefix,
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

fs::path get_user_cache_dir() {
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

} // namespace util
