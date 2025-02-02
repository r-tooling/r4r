#include "cli.h"
#include "common.h"
#include "default_image_files.h"
#include "dockerfile.h"
#include "dpkg_database.h"
#include "filesystem_trie.h"
#include "fs.h"
#include "logger.h"
#include "rpkg_database.h"
#include "syscall_monitor.h"
#include "util.h"

#include <csignal>
#include <fcntl.h>
#include <filesystem>

#include <cstdint>
#include <cstdlib>
#include <fstream>
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
    std::optional<std::uintmax_t> size{};
    bool existed_before{};

    friend std::ostream& operator<<(std::ostream& os, FileInfo const& info) {
        os << "FileInfo { " << info.path << ", size: ";
        if (info.size) {
            os << *info.size;
        } else {
            os << "N/A";
        }
        os << ", existed_before: " << info.existed_before;
        os << "}";
        return os;
    }
};

class FileTracer : public SyscallListener {
    using Warnings = std::vector<std::string>;
    using SyscallState = std::variant<std::monostate, FileInfo>;
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

    void register_file(FileInfo info) {
        auto& path = info.path;

        if (info.existed_before) {
            std::error_code ec;
            auto size = fs::file_size(path, ec);

            if (ec) {
                register_warning(STR("Failed to get file size of:  "
                                     << path << ": " << ec.message()));
            } else {
                info.size = size;
            }
        }

        files_.try_emplace(path, info);
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

        std::error_code ec;
        bool exists = fs::exists(result, ec);
        if (ec) {
            LOG_WARN(log_) << "Failed to check if file exists: " << result
                           << ": " << ec.message();
            return;
        }
        state = FileInfo{.path = result, .existed_before = exists};
    }

    void generic_open_exit([[maybe_unused]] pid_t pid, SyscallRet ret_val,
                           bool is_error, SyscallState const& state) {
        if (is_error) {
            return;
        }

        if (std::holds_alternative<FileInfo>(state)) {
            auto& info = std::get<FileInfo>(state);
            auto& entry_file = info.path;

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
                    register_file(info);
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
        auto path =
            SyscallMonitor::read_string_from_process(pid, args[0], PATH_MAX);
        state = FileInfo{.path = path};
    }

    void syscall_execve_exit(pid_t, SyscallRet, bool is_error,
                             SyscallState const& state) {
        if (!is_error) {
            if (std::holds_alternative<FileInfo>(state)) {
                auto info = std::get<FileInfo>(state);
                // it succeeded
                info.existed_before = true;

                LOG_DEBUG(log_) << "execve " << info.path;

                register_file(info);
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

    static inline Logger& log_ = LogManager::logger("file-tracer");
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
        LOG_INFO(log) << "Tracing program: " << string_join(cmd_, ' ');

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
    fs::path R_bin{"R"};
    std::vector<std::string> cmd;
    std::string docker_base_image{"ubuntu:22.04"};
    fs::path output_dir{"."};
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

class ManifestPart {
  public:
    virtual ~ManifestPart() = default;

    virtual void load_from_files(std::vector<FileInfo>&) = 0;
    virtual void load_from_manifest(std::istream&){};
    virtual void write_to_manifest(std::ostream&) = 0;
    virtual void write_to_docker(DockerFileBuilder&){};

  protected:
    static bool is_section_header(std::string const& line) {
        static std::regex const section_regex(R"(^\w+:$)");
        return std::regex_match(line, section_regex);
    }
};

class DebPackagesManifest : public ManifestPart {
  public:
    explicit DebPackagesManifest(
        DpkgDatabase dpkg_database = DpkgDatabase::system_database())
        : dpkg_database_(std::move(dpkg_database)) {}

    void load_from_files(std::vector<FileInfo>& files) override {
        SymlinkResolver symlink_resolver{};

        packages_.clear();
        files_.clear();

        auto resolved = [&](FileInfo const& info) {
            auto path = info.path;
            for (auto& p : symlink_resolver.get_root_symlink(path)) {
                std::cerr << "**********" << p << "\n";
                if (auto* pkg = dpkg_database_.lookup_by_path(p); pkg) {

                    LOG_DEBUG(log_)
                        << "resolved: " << path << " to: " << pkg->name;

                    auto it = packages_.insert(pkg);
                    files_.insert_or_assign(p, *it.first);

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

    void load_from_manifest(std::istream& input) override {
        packages_.clear();
        files_.clear();

        std::string line;
        bool inside_section = false;

        while (std::getline(input, line)) {
            line = string_trim(line);

            if (line.empty())
                continue;

            if (!inside_section) {
                if (line == "ubuntu:") {
                    inside_section = true;
                }
                continue;
            }

            if (is_section_header(line)) {
                break;
            }

            if (line.starts_with("-")) {
                std::istringstream lineStream(line.substr(1)); // Skip '-'
                std::string package_name, version;
                lineStream >> package_name >> version;

                package_name = string_trim(package_name);
                version = string_trim(version);

                if (package_name.empty() || version.empty()) {
                    throw std::runtime_error("Invalid package format: " + line);
                }

                auto* pkg = dpkg_database_.lookup_by_name(package_name);
                if (pkg) {
                    if (pkg->version != version) {
                        LOG_WARN(log_)
                            << "Package version mismatch: " << package_name
                            << " manifest: " << version
                            << " installed: " << pkg->version;
                    } else {
                        packages_.insert(pkg);
                    }
                } else {
                    LOG_WARN(log_) << "Package not found: " << package_name;
                }
            } else {
                throw std::runtime_error(
                    "Invalid format: Expected lines starting with '-' or '- '");
            }
        }

        LOG_INFO(log_) << "Loaded " << packages_.size()
                       << " packages from manifest";
    }

    void write_to_manifest(std::ostream& dst) override {
        if (packages_.empty()) {
            dst << "# No ubuntu packages will be installed\n";
            return;
        }

        dst << "# The following " << packages_.size()
            << " ubuntu packages will be installed:\n";
        dst << "#\n";
        dst << "ubuntu:\n";
        for (auto const& pkg : packages_) {
            dst << "- " << pkg->name << " " << pkg->version << "\n";
        }
    }

  private:
    static inline Logger& log_ = LogManager::logger("manifest.dpkg");

    DpkgDatabase dpkg_database_;
    std::unordered_map<fs::path, DebPackage const*> files_;
    std::unordered_set<DebPackage const*> packages_;
};

class CRANPackagesManifest : public ManifestPart {
  public:
    explicit CRANPackagesManifest(RpkgDatabase rpkg_database)
        : rpkg_database_(std::move(rpkg_database)) {}

    explicit CRANPackagesManifest(fs::path const& R_bin)
        : CRANPackagesManifest(RpkgDatabase::from_R(R_bin)) {}

    void load_from_files(std::vector<FileInfo>& files) override {
        SymlinkResolver symlink_resolved{};

        auto resolved = [&](FileInfo const& info) {
            auto path = info.path;
            for (auto& p : symlink_resolved.get_root_symlink(path)) {
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

    void write_to_manifest(std::ostream& dst) override {
        if (packages_.empty()) {
            dst << "# No CRAN packages will be installed\n";
            return;
        }

        dst << "# The following " << packages_.size()
            << " CRAN packages will be installed:\n";
        dst << "#\n";
        dst << "cran:\n";
        for (auto const& pkg : packages_) {
            dst << "- " << pkg->name << " " << pkg->version << "\n";
        }
    }

  private:
    static inline Logger& log_ = LogManager::logger("manifest.rpkg");

    RpkgDatabase rpkg_database_;
    std::unordered_map<fs::path, RPackage const*> files_;
    std::unordered_set<RPackage const*> packages_;
};

class IgnoreFilesManifest : public ManifestPart {
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

    void write_to_manifest(std::ostream&) override {}

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

    static inline Logger& log_ = LogManager::logger("manifest.ignore");

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

class CopyFileManifest : public ManifestPart {
    void load_from_files(std::vector<FileInfo>& files) override {
        for (auto& f : files) {
            auto& path = f.path;
            std::string msg = STR("resolved: " << path << " to: ");

            if (!f.existed_before) {
                LOG_DEBUG(log_) << msg << "ignore - did not exist before";
                continue;
            }

            if (!fs::exists(path)) {
                LOG_DEBUG(log_) << msg << "ignore - no longer exists";
                continue;
            }

            if (fs::is_regular_file(path)) {
                std::ifstream i{path, std::ios::in};
                if (!i) {
                    LOG_DEBUG(log_) << msg << "ignore - cannot by opened";
                    continue;
                }
            }

            // TODO: if is a directory check if it can be read from
            // TODO: check size / sha1

            LOG_DEBUG(log_) << msg << "copy";

            files_.push_back(path);
        }

        // if (!unmatched_files.empty()) {
        //     util::create_tar_archive(archive, unmatched_files);
        //     df << "COPY [" << archive << ", " << archive << "]\n";
        //     df << "RUN tar -x --file " << archive
        //        << " --absolute-names && rm -f " << archive << "\n";
        //     df << "\n";
        // }
    }

    void write_to_manifest(std::ostream& dst) override {
        if (files_.empty()) {
            dst << "# No files will ne copies\n";
            return;
        }

        dst << "# The following " << files_.size() << " will be copied:\n";
        dst << "#\n";
        dst << "copy:\n";
        for (auto const& file : files_) {
            dst << "- " << file << "\n";
        }
    }

  private:
    fs::path archive_;
    std::vector<fs::path> files_;
    static inline Logger& log_ = LogManager::logger("manifest.copy");
};

using Manifest = std::vector<std::unique_ptr<ManifestPart>>;

class ManifestTask : public Task<Manifest> {
  public:
    explicit ManifestTask(Options const& options, std::vector<FileInfo>& files)
        : Task{"manifest"}, options_{options}, files_{files} {}

  private:
    Manifest run(Logger& log, [[maybe_unused]] std::ostream& ostream) override {

        Manifest manifests;
        manifests.push_back(std::make_unique<IgnoreFilesManifest>());
        manifests.push_back(std::make_unique<DebPackagesManifest>());
        manifests.push_back(
            std::make_unique<CRANPackagesManifest>(options_.R_bin));
        manifests.push_back(std::make_unique<CopyFileManifest>());

        for (auto& m : manifests) {
            LOG_INFO(log) << "Resolving " << files_.size() << " files";
            m->load_from_files(files_);
        }

        auto manifest_file = TempFile{"r4r-manifest", ".conf"};
        {
            std::ofstream manifest_content{*manifest_file};

            manifest_content
                << "# This is the manifest file generated by r4r\n"
                << "# You can update its content by either adding or "
                   "removing lines in the corresponding parts\n";

            for (auto& m : manifests) {
                m->write_to_manifest(manifest_content);
            }
        }

        LOG_DEBUG(log) << "Writing manifest to: " << *manifest_file;
        auto ts = fs::last_write_time(*manifest_file);

        if (open_manifest(*manifest_file) &&
            fs::last_write_time(*manifest_file) != ts) {

            LOG_DEBUG(log) << "Rereading manifest from: " << *manifest_file;
            auto updated_manifest_content = read_manifest(*manifest_file);

            std::istringstream iss(updated_manifest_content);
            for (auto& m : manifests) {
                m->load_from_manifest(iss);
            }
        }

        return manifests;
    }

  private:
    static std::string read_manifest(fs::path const& path) {
        std::string input = read_from_file(path);
        std::istringstream iss(input);
        std::ostringstream oss;
        std::string line;
        bool first = true;

        while (std::getline(iss, line)) {
            line = string_trim(line);
            if (line.empty() || line.starts_with('#')) {
                continue;
            }

            if (!first) {
                oss << '\n';
            }
            oss << line;
            first = false;
        }

        return oss.str();
    }

    bool open_manifest(fs::path const& path) {
        char const* editor = std::getenv("VISUAL");
        if (!editor) {
            editor = std::getenv("EDITOR");
        }
        if (!editor) {
            LOG_ERROR(log_) << "No editor found. Set VISUAL or EDITOR "
                               "environment variable.";
            return false;
        }

        std::string command = STR(editor << " " << path.string());

        LOG_DEBUG(log_) << "Opening the manifest file: " << command;
        int status = std::system(command.c_str());
        if (status == -1) {
            LOG_ERROR(log_) << "Failed to open the manifest file: " << command;
            return false;
        }

        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            int exit_code = WEXITSTATUS(status);
            LOG_DEBUG(log_)
                << "Editor: " << command << " exit code: " << exit_code;
            return false;
        }

        if (WIFSIGNALED(status)) {
            int signal = WTERMSIG(status);
            LOG_DEBUG(log_)
                << "Editor: " << command << " terminated by signal: " << signal;
            return false;
        }

        return true;
    }

    static inline Logger& log_ = LogManager::logger("manifest");
    Options const& options_;
    std::vector<FileInfo>& files_;
};

class DockerFileBuilderTask : public Task<DockerFile> {
  public:
    DockerFileBuilderTask(Options const& options, Environment const& envir,
                          Manifest const& manifest)
        : Task{"dockerfile-builder"}, options_{options}, envir_{envir},
          manifest_{manifest} {}

    DockerFile run([[maybe_unused]] Logger& log,
                   [[maybe_unused]] std::ostream& ostream) override {

        DockerFileBuilder builder{options_.docker_base_image,
                                  options_.output_dir};

        builder.env("DEBIAN_FRONTEND", "noninteractive");
        set_locale(builder);
        // create_user(builder);
        //
        for (auto& m : manifest_) {
            m->write_to_docker(builder);
        }

        // TODO: remove LANG
        // set_environment(builder);

        return builder.build();
    }

  private:
    void set_locale(DockerFileBuilder& builder) {
        std::optional<std::string> lang = "C"s;
        if (auto it = envir_.vars.find("LANG"); it != envir_.vars.end()) {
            lang = it->second;
        }

        if (lang) {
            builder.env("LANG", *lang);
            builder.nl();

            builder.run(
                R"(apt-get update -y && \
                   apt-get install -y --no-install-recommend locales && \
                   locale-gen $LANG && \
                   update-locale LANG=$LANG)");
        }
    }

    Options const& options_;
    Environment const& envir_;
    Manifest const& manifest_;
};

class DockerImage {};

class DockerImageBuilder : public Task<DockerImage> {
  public:
    DockerImageBuilder(Options const& options, DockerFile const& docker_file)
        : Task{"docker-image-builder"}, options_{options},
          docker_file_{docker_file} {}

    DockerImage run([[maybe_unused]] Logger& log,
                    [[maybe_unused]] std::ostream& ostream) override {
        (void)options_;

        std::cout << docker_file_.dockerfile << std::endl;

        return DockerImage{};
    }

  private:
    Options const& options_;
    DockerFile const& docker_file_;
};

class Tracer {
  public:
    explicit Tracer(Options options)
        : options_{std::move(options)}, runner_{std::cout} {}

    void trace() {
        auto envir = runner_.run(CaptureEnvironmentTask{});
        auto files = runner_.run(TracingTask{options_.cmd});
        auto manifest = runner_.run(ManifestTask{options_, files});
        auto docker_file =
            runner_.run(DockerFileBuilderTask{options_, envir, manifest});
        auto docker_image =
            runner_.run(DockerImageBuilder{options_, docker_file});

        (void)docker_image;

        // rerun();
        // diff();
    }

    void stop() { runner_.stop(); }

  private:
    void trace_program() {}

    static inline Logger& log_ = LogManager::logger("tracer");
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
