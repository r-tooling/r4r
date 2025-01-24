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
    const auto mismatch =
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

UserInfo get_user_info() {

    uid_t uid = getuid(); // Get the user ID of the calling process
    gid_t gid = getgid(); // Get the group ID of the calling process

    // Retrieve the passwd struct for the user
    passwd* pwd = getpwuid(uid);
    if (!pwd) {
        throw std::runtime_error("Failed to get passwd struct for UID " +
                                 std::to_string(uid));
    }

    std::string username = pwd->pw_name;
    std::string home_directory = pwd->pw_dir;
    std::string shell = pwd->pw_shell;

    // primary group information
    group* grp = getgrgid(gid);
    if (!grp) {
        throw std::runtime_error("Failed to get group struct for GID " +
                                 std::to_string(gid));
    }
    GroupInfo primary_group = {gid, grp->gr_name};

    // Get the list of groups
    int n_groups = 0;
    getgrouplist(username.c_str(), gid, nullptr,
                 &n_groups); // Get number of groups

    std::vector<gid_t> group_ids(n_groups);
    if (getgrouplist(username.c_str(), gid, group_ids.data(), &n_groups) ==
        -1) {
        throw std::runtime_error("Failed to get group list for user " +
                                 username);
    }

    // map gids to GroupInfo
    std::vector<GroupInfo> groups;
    for (gid_t group_id : group_ids) {
        group* g = getgrgid(group_id);
        if (g) {
            groups.push_back({group_id, g->gr_name});
        }
    }

    return {uid, primary_group, username, home_directory, shell, groups};
}
} // namespace util
