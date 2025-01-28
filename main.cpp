#include "common.hpp"
#include "dpkg_database.hpp"
#include "logger.hpp"
#include <csignal>
#include <filesystem>

#include "cli.hpp"
#include "syscall_monitor.hpp"
#include "util.hpp"

#include <cstdint>
#include <cstdlib>
#include <grp.h>
#include <iostream>
#include <memory>
#include <pwd.h>
#include <sstream>
#include <stdexcept>
#include <string>

#include <cerrno>

#include <system_error>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

std::function<void(int)> global_signal_handler;

void register_signal_handlers(std::function<void(int)> handler) {
    global_signal_handler = std::move(handler);
    std::array signals = {SIGINT, SIGTERM};
    for (int sig : signals) {
        auto status = signal(sig, [](int sig) { global_signal_handler(sig); });
        if (status == SIG_ERR) {
            throw make_system_error(errno,
                                    STR("Failed to register signal "
                                        << strsignal(sig) << " handler"));
        }
    }
}

struct GroupInfo {
    gid_t gid;
    std::string name;

    friend std::ostream& operator<<(std::ostream& os, GroupInfo const& group) {
        os << "GroupInfo {\n";
        prefixed_ostream(os, "  ", [&] {
            os << "gid: " << group.gid << "\n";
            os << "name: '" << group.name << "\n";
        });
        os << "}";
        return os;
    }
};

struct UserInfo {
    uid_t uid;
    GroupInfo group;

    std::string username;
    std::string home_directory;
    std::string shell;
    std::vector<GroupInfo> groups;

    friend std::ostream& operator<<(std::ostream& os, UserInfo const& user) {
        os << "UserInfo {\n";
        prefixed_ostream(os, "  ", [&] {
            os << "uid: " << user.uid << "\n";
            os << "group: " << user.group << "\n";
            os << "username: '" << user.username << "'\n";
            os << "home_directory: '" << user.home_directory << "'\n";
            os << "shell: '" << user.shell << "'\n";
            os << "groups:\n";
            if (!user.groups.empty()) {
                prefixed_ostream(os, "  ", [&] {
                    for (auto const& g : user.groups) {
                        os << "- " << g << "\n";
                    }
                });
            }
            os << "\n";
        });
        os << "}";
        return os;
    }
};

UserInfo get_user_info() {
    uid_t uid = getuid();
    gid_t gid = getgid();

    // retrieve the passwd struct for the user
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

    // get the list of groups
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

struct FileInfo {
    fs::path path;
    std::optional<std::uintmax_t> size;

    friend std::ostream& operator<<(std::ostream& os, FileInfo const& info) {
        os << "FileInfo { " << info.path << ", size: ";
        if (info.size) {
            os << *info.size;
        } else {
            os << "N/A";
        }
        os << "}";
        return os;
    }
};

class FileTracer : public SyscallListener {
    using Warnings = std::vector<std::string>;
    using SyscallState = std::uint64_t;
    using PidState = std::pair<int, SyscallState>;

    struct SyscallHandler {
        void (FileTracer::*entry)(pid_t, SyscallArgs, SyscallState*);
        void (FileTracer::*exit)(pid_t, SyscallRet, bool, SyscallState const*);
    };

  public:
    using Files = std::unordered_map<fs::path, FileInfo>;

    explicit FileTracer(Logger log) : log_{std::move(log)} {}

    void on_syscall_entry(pid_t pid, std::uint64_t syscall,
                          SyscallArgs args) override {
        auto it = kHandlers_.find(syscall);
        if (it == kHandlers_.end()) {
            return;
        }

        auto handler = it->second;
        SyscallState state = 0;

        (this->*(handler.entry))(pid, args, &state);

        auto [s_it, inserted] = state_.try_emplace(pid, syscall, state);
        if (!inserted) {
            throw std::runtime_error(
                STR("There is already a syscall handler for pid: " << pid));
        }
    }

    void on_syscall_exit(pid_t pid, SyscallRet ret_val,
                         bool is_error) override {
        auto node = state_.extract(pid);
        if (node) {
            auto [syscall, state] = node.mapped();
            auto it = kHandlers_.find(syscall);
            if (it == kHandlers_.end()) {
                throw std::runtime_error(
                    STR("No exit handler for syscall: " << syscall));
            }
            auto handler = it->second;
            (this->*(handler.exit))(pid, ret_val, is_error, &state);
        }
    }

    Files const& files() const { return files_; }

  private:
    // FIXME: use logger
    void register_warning(std::string const& message) {
        warnings_.push_back(message);
    }

    void register_file(fs::path file) {
        auto size = util::file_size(file);
        FileInfo info{file, {}};

        if (std::holds_alternative<std::error_code>(size)) {
            register_warning(STR("Failed to get file size of:  "
                                 << file << ": "
                                 << std::get<std::error_code>(size).message()));
        } else {
            info.size = std::get<std::uintmax_t>(size);
        }

        files_.try_emplace(file, info);
    }

    void syscall_openat_entry(pid_t pid, SyscallArgs args,
                              SyscallState* state) {
        fs::path result;
        fs::path pathname =
            SyscallMonitor::read_string_from_process(pid, args[1], PATH_MAX);

        // the logic comes from the behavior of openat(2):
        if (pathname.is_absolute()) {
            result = pathname;
        } else {
            auto dirfd = static_cast<int>(args[0]);
            if (dirfd == AT_FDCWD) {
                auto d = util::get_process_cwd(pid);
                if (!d) {
                    register_warning(STR("failed to resolve cwd of: " << pid));
                    return;
                }
                result = *d;
            } else {
                auto d = util::resolve_fd_filename(pid, dirfd);
                if (!d) {
                    register_warning(STR("Failed to resolve dirfd: " << dirfd));
                    return;
                }
                result = *d;
            }
            result /= pathname;
        }

        if (fs::exists(result)) {
            auto file = new fs::path{result};
            *state = reinterpret_cast<SyscallState>(file);
        }
    }

    void syscall_openat_exit([[maybe_unused]] pid_t pid, SyscallRet ret_val,
                             bool is_error, SyscallState const* state) {
        (void)pid;

        if (is_error) {
            return;
        }

        auto entry_file = reinterpret_cast<fs::path*>(*state);
        if (entry_file) {
            if (ret_val >= 0) {
                auto exit_file =
                    util::resolve_fd_filename(pid, static_cast<int>(ret_val));

                if (!exit_file) {
                    LOG_WARN(log_)
                        << "Unable to resolve fd: " << ret_val << " to a path";
                } else if (exit_file != *entry_file) {
                    LOG_WARN(log_)
                        << "File entry/exit mismatch: " << *entry_file << " vs "
                        << *exit_file;
                } else {
                    register_file(*exit_file);
                }
            }
            delete entry_file;
        }
    }

    static inline std::unordered_map<uint64_t, SyscallHandler> const kHandlers_{
#define REG_SYSCALL_HANDLER(nr)                                                \
    {                                                                          \
        __NR_##nr, {                                                           \
            &FileTracer::syscall_##nr##_entry,                                 \
                &FileTracer::syscall_##nr##_exit                               \
        }                                                                      \
    }

        REG_SYSCALL_HANDLER(openat),

#undef REG_SYSCALL_HANDLER
    };

    Logger log_;
    std::unordered_map<pid_t, PidState> state_;
    Files files_;
    Warnings warnings_;
};

class TaskException : public std::runtime_error {
  public:
    TaskException(std::string const& message) : std::runtime_error{message} {}
};

class TracingTask : public Task<std::vector<FileInfo>> {
  public:
    TracingTask(std::vector<std::string> const& cmd)
        : Task{"trace"}, cmd_(cmd) {}

  public:
    std::vector<FileInfo> run(Logger& log, std::ostream& output) override {
        LOG_INFO(log) << "Tracing program: " << util::mk_string(cmd_, ' ');

        FileTracer tracer{log};
        SyscallMonitor monitor{cmd_, tracer};
        monitor.redirect_stdout(output);
        monitor.redirect_stderr(output);

        monitor_ = &monitor;

        auto result = monitor_->start();

        monitor_ = nullptr;

        switch (result.kind) {
        case SyscallMonitor::Result::Failure:
            throw TaskException("Failed to spawn the process");

        case SyscallMonitor::Result::Signal:
            throw TaskException(
                STR("Program was terminated by signal: " << *result.detail));

        case SyscallMonitor::Result::Exit:
            int exit_code = *result.detail;
            if (exit_code != 0) {
                throw TaskException(STR("Program exited with: " << exit_code));
            }

            auto file_map = tracer.files();
            std::vector<FileInfo> files;
            files.reserve(file_map.size());
            for (auto const& [key, value] : file_map) {
                files.push_back(value);
            }

            std::sort(files.begin(), files.end(),
                      [](auto const& lhs, auto const& rhs) {
                          return lhs.path < rhs.path;
                      });

            return files;
        }

        UNREACHABLE();
    }

    void stop() override {
        if (monitor_) {
            monitor_->stop();
        }
    }

  private:
    std::vector<std::string> const& cmd_;
    SyscallMonitor* monitor_;
};

struct Options {
    std::vector<std::string> cmd;
};

Options parse_cmd_args(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        args.emplace_back(argv[i]);
    }
    return {args};
}

struct Environment {
    fs::path cwd;
    std::unordered_map<std::string, std::string> vars;
    UserInfo user;

    friend std::ostream& operator<<(std::ostream& os,
                                    Environment const& trace) {
        os << "Environment {\n";
        prefixed_ostream(os, "  ", [&] {
            os << "'\n";
            os << "cwd: " << trace.cwd << "\n";
            os << "env:\n";
            prefixed_ostream(os, "  ", [&] {
                for (auto& [k, v] : trace.vars) {
                    os << "- " << k << ": " << util::remove_ansi(v) << "\n";
                }
            });
            os << "user: ";
            prefixed_ostream(os, "  ", [&] { os << trace.user; });
            os << "\n";
        });

        os << "}";
        return os;
    }
};

class CaptureEnvironmentTask : public Task<Environment> {
  public:
    CaptureEnvironmentTask() : Task{"capture-environment"} {}

    Environment run(Logger& log,
                    [[maybe_unused]] std::ostream& ostream) override {
        Environment envir{};

        envir.cwd = std::filesystem::current_path();
        envir.user = get_user_info();

        extern char** environ;
        if (environ != nullptr) {
            for (char** env = environ; *env != nullptr; ++env) {
                std::string s(*env);
                size_t pos = s.find('=');
                if (pos != std::string::npos) {
                    envir.vars.emplace(s.substr(0, pos), s.substr(pos + 1));
                } else {
                    LOG_WARN(log)
                        << "Invalid environment variable: '" << s << "'";
                }
            }
        } else {
            LOG_WARN(log) << "Unable to get environment variables";
        }

        return envir;
    }
};

void populate_root_symlinks(std::unordered_map<fs::path, fs::path>& symlinks) {
    fs::path root = "/";
    for (auto const& entry : fs::directory_iterator(root)) {
        if (entry.is_symlink()) {
            std::error_code ec;
            fs::path target = fs::read_symlink(entry.path(), ec);
            if (!target.is_absolute()) {
                target = fs::canonical(root / target);
            }
            if (!ec && fs::is_directory(target)) {
                symlinks[entry.path()] = target;
            }
        }
    }
}

std::vector<fs::path> get_root_symlink(fs::path const& path) {
    static std::unordered_map<fs::path, fs::path> symlinks;
    if (symlinks.empty()) {
        populate_root_symlinks(symlinks);
    }

    std::vector<fs::path> result = {path};

    for (auto const& [symlink, target] : symlinks) {
        if (util::is_sub_path(path, target)) {
            fs::path candidate = symlink / path.lexically_relative(target);

            std::error_code ec;
            if (fs::exists(candidate, ec) &&
                fs::equivalent(candidate, path, ec)) {
                result.push_back(candidate);
                break;
            }
        }
    }

    return result;
}

class Manifest {
  public:
    virtual ~Manifest() = default;

    virtual void load_from_files(std::vector<FileInfo>& files) = 0;

    // virtual void load_from_string(const std::string& text) = 0;
    // virtual std::string to_text() const = 0;
};

class DebPackagesManifest : public Manifest {
  public:
    DebPackagesManifest(Logger log, DpkgDatabase dpkg_database =
                                        DpkgDatabase::system_database())
        : log_{log}, dpkg_database_(dpkg_database) {}

    virtual void load_from_files(std::vector<FileInfo>& files) {
        auto resolved = [&](FileInfo const& info) {
            auto path = info.path;
            for (auto& p : get_root_symlink(path)) {
                if (auto* pkg = dpkg_database_.lookup_by_path(p); pkg) {

                    LOG_DEBUG(log_)
                        << "resolved: " << path << " to: " << pkg->name;

                    auto it = packages_.insert(*pkg);
                    files_.insert_or_assign(p, &(*it.first));

                    // TODO: check the size

                    return true;
                }
            }
            return false;
        };

        std::erase_if(files, resolved);
        LOG_INFO(log_) << "Resolved " << files_.size() << " to "
                       << packages_.size() << " debian packages";
    };

  private:
    Logger log_;
    DpkgDatabase dpkg_database_;
    std::unordered_map<fs::path, DebPackage const*> files_;
    std::unordered_set<DebPackage> packages_;
};

using Manifests = std::vector<std::unique_ptr<Manifest>>;

class ManifestsTask : public Task<Manifests> {
  public:
    explicit ManifestsTask(std::vector<FileInfo>& files)
        : Task{"manifest"}, files_{files} {}

  private:
    Manifests run(Logger& log,
                  [[maybe_unused]] std::ostream& ostream) override {
        Manifests manifests;
        manifests.push_back(
            std::make_unique<DebPackagesManifest>(Logger{"deb-manifest", log}));

        LOG_INFO(log) << "Resolving " << files_.size() << " files";
        for (auto& m : manifests) {
            m->load_from_files(files_);
        }

        return manifests;
    }

  private:
    std::vector<FileInfo>& files_;
};

class Tracer {
  public:
    Tracer(Options options)
        : options_{options}, log_{"r4r"}, runner_{std::cout} {}

    void trace() {
        auto envir = runner_.run(CaptureEnvironmentTask{});
        auto files = runner_.run(TracingTask{options_.cmd});
        auto manifests = runner_.run(ManifestsTask{files});

        // resolve_files();
        // create_dockerfile();
        // build_docker_image();
        // rerun();
        // diff();

        // std::cout << envir;
        // util::print_collection(std::cout, files, '\n');
    }

    void stop() { runner_.stop(); }

  private:
    void trace_program() {}

    Options options_;
    Logger log_;
    TaskRunner runner_;
};

int main(int argc, char* argv[]) {
    Options options = parse_cmd_args(argc, argv);
    Tracer tracer{options};

    // Interrupt signals generated in the terminal are delivered to the
    // active process group, which here includes both parent and child. A
    // signal manually generated and sent to an individual process (perhaps
    // with kill) will be delivered only to that process, regardless of
    // whether it is the parent or child. That is why we need to register a
    // signal handler that will terminate the tracee when the tracer
    // gets killed.

    register_signal_handlers([&, got_sigint = false](int sig) mutable {
        switch (sig) {
        case SIGTERM:
            tracer.stop();
            exit(1);
        case SIGINT:
            if (got_sigint) {
                std::cerr << "SIGINT twice, exiting the tracer!";
                exit(1);
            } else {
                std::cerr << "SIGINT, stopping the current task...";
                tracer.stop();
                got_sigint = true;
            }
            break;
        default:
            UNREACHABLE();
        }
    });

    try {
        tracer.trace();
        return 0;
    } catch (TaskException& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
