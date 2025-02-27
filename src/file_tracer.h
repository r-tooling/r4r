#ifndef FILE_TRACER_H
#define FILE_TRACER_H

#include "filesystem_trie.h"
#include "logger.h"
#include "syscall_monitor.h"
#include <fcntl.h>
#include <filesystem>
#include <system_error>
#include <variant>

struct FileInfo {
    fs::path path;
    std::optional<std::uintmax_t> size;
    bool existed_before;
};

// Use classes for each syscall with static methods and a ref to FileTracer
class FileTracer : public SyscallListener {
  public:
    using Files = std::unordered_map<fs::path, FileInfo>;

    // FIXME: what is the idiomatic way? use pointers instead?
    explicit FileTracer(FileSystemTrie<bool> const&&) = delete;
    explicit FileTracer(
        FileSystemTrie<bool> const& ignore_file_list = kNoIgnoreFiles_)
        : ignore_file_list_{ignore_file_list} {}

    void on_syscall_entry(pid_t pid, std::uint64_t syscall,
                          SyscallArgs args) override;

    void on_syscall_exit(pid_t pid, SyscallRet ret_val, bool is_error) override;

    Files const& files() const { return files_; }

    std::uint64_t syscalls_count() const { return syscalls_count_; }

  private:
    static inline FileSystemTrie<bool> const kNoIgnoreFiles_;

    using Warnings = std::vector<std::string>;
    using SyscallState = std::variant<std::monostate, FileInfo>;
    using PidState = std::pair<int, SyscallState>;

    struct SyscallHandler {
        void (FileTracer::*entry)(pid_t, SyscallArgs, SyscallState&);
        void (FileTracer::*exit)(pid_t, SyscallRet, bool, SyscallState const&);
    };

    void register_file(FileInfo info);

    void generic_open_entry(pid_t pid, int dirfd, fs::path const& pathname,
                            SyscallState& state);

    void generic_open_exit([[maybe_unused]] pid_t pid, SyscallRet ret_val,
                           bool is_error, SyscallState const& state);

    void syscall_openat_entry(pid_t pid, SyscallArgs args, SyscallState& state);

    void syscall_openat_exit(pid_t pid, SyscallRet ret_val, bool is_error,
                             SyscallState const& state);

    void syscall_open_entry(pid_t pid, SyscallArgs args, SyscallState& state);

    void syscall_open_exit(pid_t pid, SyscallRet ret_val, bool is_error,
                           SyscallState const& state);

    void syscall_execve_entry(pid_t pid, SyscallArgs args, SyscallState& state);

    void syscall_execve_exit(pid_t /*unused*/, SyscallRet /*unused*/,
                             bool is_error, SyscallState const& state);

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

    std::reference_wrapper<FileSystemTrie<bool> const> ignore_file_list_;
    std::uint64_t syscalls_count_{0};
    std::unordered_map<pid_t, PidState> state_;
    Files files_;
    Warnings warnings_;
};

inline void FileTracer::on_syscall_entry(pid_t pid, std::uint64_t syscall,
                                         uint64_t* args) {
    LOG(TRACE) << "Syscall entry: " << syscall << " pid: " << pid;

    syscalls_count_++;

    auto it = kHandlers_.find(syscall);
    if (it == kHandlers_.end()) {
        return;
    }

    auto [entry, _] = it->second;
    SyscallState state = std::monostate{};

    (this->*(entry))(pid, args, state);

    auto [s_it, inserted] = state_.try_emplace(pid, syscall, state);

    if (!inserted) {
        throw std::runtime_error(
            STR("There is already a syscall handler for pid: " << pid));
    }
}

inline void FileTracer::on_syscall_exit(pid_t pid, SyscallRet ret_val,
                                        bool is_error) {
    LOG(TRACE) << "Syscall exit: pid: " << pid;

    if (auto node = state_.extract(pid)) {
        auto [syscall, state] = node.mapped();
        auto it = kHandlers_.find(syscall);
        if (it == kHandlers_.end()) {
            throw std::runtime_error(
                STR("No exit handler for syscall: " << syscall));
        }
        auto [_, exit] = it->second;
        (this->*(exit))(pid, ret_val, is_error, state);
    }
}

inline void FileTracer::register_file(FileInfo info) {
    std::error_code ec;
    auto& path = info.path;
    if (!path.is_absolute()) {
        path = fs::absolute(path, ec);
        if (ec) {
            LOG(WARN) << "Failed to resolve path:  " << info.path << " - "
                      << ec.message();
        }
    }

    if (info.existed_before && fs::is_regular_file(path)) {
        auto size = fs::file_size(path, ec);

        if (ec) {
            LOG(WARN) << "Failed to get file size of:  " << path << " - "
                      << ec.message();
        } else {
            info.size = size;
        }
    }

    files_.try_emplace(path, info);
}

inline void FileTracer::generic_open_entry(pid_t pid, int dirfd,
                                           fs::path const& pathname,
                                           FileTracer::SyscallState& state) {
    fs::path result;

    LOG(TRACE) << "Syscall open " << pathname;

    // the logic comes from the behavior of openat(2):
    if (pathname.is_absolute()) {
        result = pathname;
    } else {
        if (dirfd == AT_FDCWD) {
            auto d = get_process_cwd(pid);
            if (!d) {
                LOG(WARN) << "Failed to resolve cwd of: " << pid;
                return;
            }
            result = *d;
        } else {
            auto d = resolve_fd_filename(pid, dirfd);
            if (!d) {
                LOG(WARN) << "Failed to resolve dir fd: " << dirfd;
                return;
            }
            result = *d;
        }

        std::error_code ec;
        result = fs::absolute(result, ec);
        if (ec) {
            LOG(WARN) << "Failed to resolve absolute file path : " << result
                      << " - " << ec.message();
        }

        result /= pathname;
    }

    if (bool const* it = ignore_file_list_.get().find_last_matching(result);
        it && *it) {
        LOG(DEBUG) << "Ignoring file: " << result;
        return;
    }

    std::error_code ec;
    bool exists = fs::exists(result, ec);
    if (ec) {
        LOG(WARN) << "Failed to check if file exists: " << result << " - "
                  << ec.message();
        return;
    }

    state = FileInfo{.path = result, .size = {}, .existed_before = exists};
}

void FileTracer::generic_open_exit(pid_t pid, SyscallRet ret_val, bool is_error,
                                   FileTracer::SyscallState const& state) {
    if (is_error) {
        return;
    }

    if (std::holds_alternative<FileInfo>(state)) {
        auto const& info = std::get<FileInfo>(state);
        auto const& entry_file = info.path;

        std::error_code ec;
        if (!fs::exists(entry_file, ec)) {
            return;
        }

        fs::file_status fs = fs::status(entry_file, ec);
        if (!(fs::is_regular_file(fs) || fs::is_directory(fs) ||
              fs::is_symlink(fs))) {
            LOG(WARN) << "Unsupported file type: " << entry_file << " "
                      << fs::exists(entry_file, ec);
            return;
        }

        if (ret_val >= 0) {
            auto exit_file =
                resolve_fd_filename(pid, static_cast<int>(ret_val));

            if (!exit_file) {
                LOG(WARN) << "Failed to resolve fd to a path: " << ret_val;
            } else if (!fs::equivalent(*exit_file, entry_file, ec)) {
                if (ec) {
                    LOG(WARN) << "Failed to comparable files: " << entry_file
                              << " vs " << *exit_file << " - " << ec.message();
                } else {
                    LOG(WARN) << "File entry/exit mismatch: " << entry_file
                              << " vs " << *exit_file;
                }
            } else {
                register_file(info);
            }
        }
    }
}

inline void FileTracer::syscall_openat_entry(pid_t pid, SyscallArgs args,
                                             SyscallState& state) {
    auto pathname =
        SyscallMonitor::read_string_from_process(pid, args[1], PATH_MAX);
    generic_open_entry(pid, static_cast<int>(args[0]), pathname, state);
}

inline void FileTracer::syscall_openat_exit(pid_t pid, SyscallRet ret_val,
                                            bool is_error,
                                            SyscallState const& state) {
    generic_open_exit(pid, ret_val, is_error, state);
}

inline void FileTracer::syscall_open_entry(pid_t pid, SyscallArgs args,
                                           SyscallState& state) {
    auto pathname =
        SyscallMonitor::read_string_from_process(pid, args[0], PATH_MAX);
    generic_open_entry(pid, AT_FDCWD, pathname, state);
}

inline void FileTracer::syscall_open_exit(pid_t pid, SyscallRet ret_val,
                                          bool is_error,
                                          SyscallState const& state) {
    generic_open_exit(pid, ret_val, is_error, state);
}

inline void FileTracer::syscall_execve_entry(pid_t pid, SyscallArgs args,
                                             SyscallState& state) {
    auto path =
        SyscallMonitor::read_string_from_process(pid, args[0], PATH_MAX);
    state = FileInfo{.path = path, .size = {}, .existed_before = false};
}

inline void FileTracer::syscall_execve_exit(pid_t, SyscallRet, bool is_error,
                                            SyscallState const& state) {
    if (!is_error) {
        if (std::holds_alternative<FileInfo>(state)) {
            auto info = std::get<FileInfo>(state);
            // it succeeded so it has to exist
            info.existed_before = true;

            LOG(DEBUG) << "Syscall execve " << info.path;

            register_file(info);
        } else {
            throw std::runtime_error("execve successful, yet not path stored");
        }
    }
}

#endif // FILE_TRACER_H
