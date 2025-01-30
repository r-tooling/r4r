#include "cli.h"
#include "common.h"
#include "default_image_files.h"
#include "dpkg_database.h"
#include "filesystem_trie.h"
#include "logger.h"
#include "rpkg_database.h"
#include "syscall_monitor.h"
#include "util.h"

#include <csignal>
#include <fcntl.h>
#include <filesystem>

#include <cstdint>
#include <cstdlib>
#include <grp.h>
#include <iostream>
#include <memory>
#include <pwd.h>
#include <stdexcept>
#include <string>

#include <cerrno>

#include <system_error>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// FIXME: use assert instead of runtime_exception

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
    using SyscallState = std::variant<std::monostate, fs::path>;
    using PidState = std::pair<int, SyscallState>;

    struct SyscallHandler {
        void (FileTracer::*entry)(pid_t, SyscallArgs, SyscallState&);
        void (FileTracer::*exit)(pid_t, SyscallRet, bool, SyscallState const&);
    };

  public:
    using Files = std::unordered_map<fs::path, FileInfo>;

    void on_syscall_entry(pid_t pid, std::uint64_t syscall,
                          SyscallArgs args) override {
        LOG_TRACE(log_) << "syscall_entry: " << syscall << " pid: " << pid;

        auto it = kHandlers_.find(syscall);
        if (it == kHandlers_.end()) {
            return;
        }

        auto handler = it->second;
        SyscallState state = std::monostate{};

        (this->*(handler.entry))(pid, args, state);

        auto [s_it, inserted] = state_.try_emplace(pid, syscall, state);

        if (!inserted) {
            throw std::runtime_error(
                STR("There is already a syscall handler for pid: " << pid));
        }
    }

    void on_syscall_exit(pid_t pid, SyscallRet ret_val,
                         bool is_error) override {
        LOG_TRACE(log_) << "syscall_exit: pid: " << pid;

        auto node = state_.extract(pid);
        if (node) {
            auto [syscall, state] = node.mapped();
            auto it = kHandlers_.find(syscall);
            if (it == kHandlers_.end()) {
                throw std::runtime_error(
                    STR("No exit handler for syscall: " << syscall));
            }
            auto handler = it->second;
            (this->*(handler.exit))(pid, ret_val, is_error, state);
        }
    }

    Files const& files() const { return files_; }

  private:
    // FIXME: use logger
    void register_warning(std::string const& message) {
        warnings_.push_back(message);
    }

    void register_file(fs::path const& file) {
        auto size = file_size(file);
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

    void generic_open_entry(pid_t pid, int dirfd, fs::path const& pathname,
                            SyscallState& state) {
        fs::path result;

        LOG_DEBUG(log_) << "open " << pathname;

        // the logic comes from the behavior of openat(2):
        if (pathname.is_absolute()) {
            result = pathname;
        } else {
            if (dirfd == AT_FDCWD) {
                auto d = get_process_cwd(pid);
                if (!d) {
                    register_warning(STR("failed to resolve cwd of: " << pid));
                    return;
                }
                result = *d;
            } else {
                auto d = resolve_fd_filename(pid, dirfd);
                if (!d) {
                    register_warning(STR("Failed to resolve dirfd: " << dirfd));
                    return;
                }
                result = *d;
            }
            result /= pathname;
        }

        if (fs::exists(result)) {
            state = result;
        }
    }

    void generic_open_exit([[maybe_unused]] pid_t pid, SyscallRet ret_val,
                           bool is_error, SyscallState const& state) {
        if (is_error) {
            return;
        }

        if (std::holds_alternative<fs::path>(state)) {
            fs::path entry_file = std::get<fs::path>(state);
            if (ret_val >= 0) {
                auto exit_file =
                    resolve_fd_filename(pid, static_cast<int>(ret_val));

                std::error_code ec;
                if (!exit_file) {
                    LOG_WARN(log_)
                        << "Unable to resolve fd: " << ret_val << " to a path";
                } else if (!fs::equivalent(*exit_file, entry_file, ec)) {
                    if (ec) {
                        LOG_WARN(log_)
                            << "File entry/exit not-comparable: " << entry_file
                            << " vs " << *exit_file << ": " << ec.message();
                    } else {
                        LOG_WARN(log_)
                            << "File entry/exit mismatch: " << entry_file
                            << " vs " << *exit_file;
                    }
                } else {
                    register_file(*exit_file);
                }
            }
        }
    }

    void syscall_openat_entry(pid_t pid, SyscallArgs args,
                              SyscallState& state) {
        auto pathname =
            SyscallMonitor::read_string_from_process(pid, args[1], PATH_MAX);

        generic_open_entry(pid, static_cast<int>(args[0]), pathname, state);
    }

    void syscall_openat_exit(pid_t pid, SyscallRet ret_val, bool is_error,
                             SyscallState const& state) {
        generic_open_exit(pid, ret_val, is_error, state);
    }

    void syscall_open_entry(pid_t pid, SyscallArgs args, SyscallState& state) {
        auto pathname =
            SyscallMonitor::read_string_from_process(pid, args[1], PATH_MAX);
        generic_open_entry(pid, AT_FDCWD, pathname, state);
    }

    void syscall_open_exit(pid_t pid, SyscallRet ret_val, bool is_error,
                           SyscallState const& state) {
        generic_open_exit(pid, ret_val, is_error, state);
    }

    void syscall_execve_entry(pid_t pid, SyscallArgs args,
                              SyscallState& state) {
        state =
            SyscallMonitor::read_string_from_process(pid, args[0], PATH_MAX);
    }

    void syscall_execve_exit(pid_t, SyscallRet, bool is_error,
                             SyscallState const& state) {
        if (!is_error) {
            if (std::holds_alternative<fs::path>(state)) {
                auto path = std::get<fs::path>(state);
                LOG_DEBUG(log_) << "execve " << path;
                register_file(path);
            } else {
                throw std::runtime_error(
                    "execve successful, yet not path stored");
            }
        }
    }

    // TODO: won't classes be easier? Passing a pointer to this?
    static inline std::unordered_map<uint64_t, SyscallHandler> const kHandlers_{
#define REG_SYSCALL_HANDLER(nr)                                                \
    {                                                                          \
        __NR_##nr, {                                                           \
            &FileTracer::syscall_##nr##_entry,                                 \
                &FileTracer::syscall_##nr##_exit                               \
        }                                                                      \
    }

        REG_SYSCALL_HANDLER(open),
        REG_SYSCALL_HANDLER(openat),
        REG_SYSCALL_HANDLER(execve),

#undef REG_SYSCALL_HANDLER
    };

    static inline Logger log_ = LogManager::logger("file-tracer");
    std::unordered_map<pid_t, PidState> state_;
    Files files_;
    Warnings warnings_;
};

class TaskException : public std::runtime_error {
  public:
    explicit TaskException(std::string const& message)
        : std::runtime_error{message} {}
};

class TracingTask : public Task<std::vector<FileInfo>> {
  public:
    explicit TracingTask(std::vector<std::string> const& cmd)
        : Task{"trace"}, cmd_(cmd) {}

  public:
    std::vector<FileInfo> run(Logger& log, std::ostream& output) override {
        LOG_INFO(log) << "Tracing program: " << mk_string(cmd_, ' ');

        FileTracer tracer;
        SyscallMonitor monitor{cmd_, tracer};
        monitor.redirect_stdout(output);
        monitor.redirect_stderr(output);

        // this is just to support the stop()
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
    SyscallMonitor* monitor_{};
};

struct Options {
    fs::path R_bin = "R";
    std::vector<std::string> cmd;
};

Options parse_cmd_args(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        args.emplace_back(argv[i]);
    }
    return {.cmd = args};
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
                    os << "- " << k << ": " << remove_ansi(v) << "\n";
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
        if (is_sub_path(path, target)) {
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
    explicit DebPackagesManifest(
        DpkgDatabase dpkg_database = DpkgDatabase::system_database())
        : dpkg_database_(std::move(dpkg_database)) {}

    void load_from_files(std::vector<FileInfo>& files) override {
        auto resolved = [&](FileInfo const& info) {
            auto path = info.path;
            for (auto& p : get_root_symlink(path)) {
                if (auto* pkg = dpkg_database_.lookup_by_path(p); pkg) {

                    LOG_DEBUG(log_)
                        << "resolved: " << path << " to: " << pkg->name;

                    auto it = packages_.insert(*pkg);
                    files_.insert_or_assign(p, &(*it.first));

                    // TODO: check that the size is the same

                    return true;
                }
            }
            return false;
        };

        std::erase_if(files, resolved);
        LOG_INFO(log_) << "Resolved " << files_.size() << " files to "
                       << packages_.size() << " debian packages";
    };

  private:
    static inline Logger log_ = LogManager::logger("manifest.dpkg");

    DpkgDatabase dpkg_database_;
    std::unordered_map<fs::path, DebPackage const*> files_;
    std::unordered_set<DebPackage> packages_;
};

class CRANPackagesManifest : public Manifest {
  public:
    explicit CRANPackagesManifest(RpkgDatabase rpkg_database)
        : rpkg_database_(std::move(rpkg_database)) {}

    explicit CRANPackagesManifest(fs::path const& R_bin)
        : CRANPackagesManifest(RpkgDatabase::from_R(R_bin)) {}

    void load_from_files(std::vector<FileInfo>& files) override {
        auto resolved = [&](FileInfo const& info) {
            auto path = info.path;
            for (auto& p : get_root_symlink(path)) {
                if (auto* pkg = rpkg_database_.lookup_by_path(p); pkg) {

                    LOG_DEBUG(log_)
                        << "resolved: " << path << " to: " << pkg->name;

                    auto it = packages_.insert(pkg);
                    files_.insert_or_assign(p, *it.first);

                    return true;
                }
            }
            return false;
        };

        std::erase_if(files, resolved);
        LOG_INFO(log_) << "Resolved " << files_.size() << " files to "
                       << packages_.size() << " R packages";
    };

  private:
    static inline Logger log_ = LogManager::logger("manifest.rpkg");

    RpkgDatabase rpkg_database_;
    std::unordered_map<fs::path, RPackage const*> files_;
    std::unordered_set<RPackage const*> packages_;
};

class IgnoreFilesManifest : public Manifest {
  public:
    void load_from_files(std::vector<FileInfo>& files) override {
        std::erase_if(files, [&](FileInfo const& info) {
            auto& path = info.path;
            if (*kIgnoredFiles.find_last_matching(path)) {
                LOG_DEBUG(log_) << "resolving: " << path << " to: ignored";
                return true;
            }
            return false;
        });

        std::erase_if(files, [&](FileInfo const& info) {
            auto& path = info.path;
            if (auto f = kDefaultImageFiles.find(path); f) {
                // TODO: check the size, perm, ...
                LOG_DEBUG(log_)
                    << "resolving: " << path << " to: ignored - image default";
                return true;
            }
            return false;
        });

        // ignore the .uuid files from fontconfig
        static std::unordered_set<fs::path> const fontconfig_dirs = {
            "/usr/share/fonts", "/usr/share/poppler", "/usr/share/texmf/fonts"};

        std::erase_if(files, [&](FileInfo const& info) {
            auto& path = info.path;
            for (auto const& d : fontconfig_dirs) {
                if (is_sub_path(path, d)) {
                    if (path.filename() == ".uuid") {
                        LOG_DEBUG(log_)
                            << "resolving: " << path << " to: ignored";
                        return true;
                    }
                }
            }
            return false;
        });
    }

  private:
    static FileSystemTrie<ImageFileInfo> load_default_files() {
        auto default_files = []() {
            if (fs::exists(kImageFileCache)) {
                return DefaultImageFiles::from_file(kImageFileCache);
            } else {
                LOG_INFO(log_)
                    << "Default image file cache " << kImageFileCache
                    << " does not exists, creating from image " << kImageName;

                auto files = DefaultImageFiles::from_image(kImageName,
                                                           kBlacklistPatterns);
                try {
                    fs::create_directories(kImageFileCache.parent_path());
                    std::ofstream out{kImageFileCache};
                    files.save(out);
                } catch (std::exception const& e) {
                    LOG_WARN(log_)
                        << "Unable to store default image file list to "
                        << kImageFileCache << ": " << e.what();
                }
                return files;
            }
        }();

        LOG_DEBUG(log_) << "Loaded " << default_files.size()
                        << " default files";

        FileSystemTrie<ImageFileInfo> trie;
        for (auto& info : default_files.files()) {
            trie.insert(info.path, info);
        }
        return trie;
    }

    static inline Logger log_ = LogManager::logger("manifest.ignore");

    static inline std::string const kImageName = "ubuntu:22.04";

    static inline fs::path const kImageFileCache = []() {
        return get_user_cache_dir() / "r4r" / (kImageName + ".cache");
    }();

    static inline std::vector<std::string> const kBlacklistPatterns = {
        "/dev/*", "/sys/*", "/proc/*"};

    static inline FileSystemTrie<ImageFileInfo> const kDefaultImageFiles =
        load_default_files();

    static inline FileSystemTrie<bool> kIgnoredFiles = [] {
        FileSystemTrie<bool> trie{false};
        trie.insert("/dev", true);
        trie.insert("/etc/ld.so.cache", true);
        trie.insert("/etc/nsswitch.conf", true);
        trie.insert("/etc/passwd", true);
        trie.insert("/proc", true);
        trie.insert("/sys", true);
        // created by locale-gen
        trie.insert("/usr/lib/locale/locale-archive", true);
        // fonts should be installed from a package
        trie.insert("/usr/local/share/fonts", true);
        // this might be a bit too drastic, but cache is usually not
        // transferable anyway
        trie.insert("/var/cache", true);
        return trie;
    }();
};

using Manifests = std::vector<std::unique_ptr<Manifest>>;

class ManifestsTask : public Task<Manifests> {
  public:
    explicit ManifestsTask(Options const& options, std::vector<FileInfo>& files)
        : Task{"manifest"}, options_{options}, files_{files} {}

  private:
    Manifests run(Logger& log,
                  [[maybe_unused]] std::ostream& ostream) override {

        Manifests manifests;
        manifests.push_back(std::make_unique<IgnoreFilesManifest>());
        manifests.push_back(std::make_unique<DebPackagesManifest>());
        manifests.push_back(
            std::make_unique<CRANPackagesManifest>(options_.R_bin));

        for (auto& m : manifests) {
            LOG_INFO(log) << "Resolving " << files_.size() << " files";
            m->load_from_files(files_);
        }

        for (auto& p : files_) {
            LOG_DEBUG(log) << "Unresolved " << p;
        }

        return manifests;
    }

  private:
    Options const& options_;
    std::vector<FileInfo>& files_;
};

class Tracer {
  public:
    explicit Tracer(Options options)
        : options_{std::move(options)}, runner_{std::cout} {}

    void trace() {
        auto envir = runner_.run(CaptureEnvironmentTask{});
        auto files = runner_.run(TracingTask{options_.cmd});
        auto manifests = runner_.run(ManifestsTask{options_, files});

        // resolve_files();
        // create_dockerfile();
        // build_docker_image();
        // rerun();
        // diff();

        // std::cout << envir;
        // print_collection(std::cout, files, '\n');
    }

    void stop() { runner_.stop(); }

  private:
    void trace_program() {}

    static inline Logger log_ = LogManager::logger("tracer");
    Options options_;
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
