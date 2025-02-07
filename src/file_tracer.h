#ifndef FILE_TRACER_H
#define FILE_TRACER_H

#include "logger.h"
#include "syscall_monitor.h"
#include <fcntl.h>
#include <filesystem>
#include <variant>

struct FileInfo {
    fs::path path;
    std::optional<std::uintmax_t> size;
    bool existed_before;
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
                          SyscallArgs args) override;

    void on_syscall_exit(pid_t pid, SyscallRet ret_val, bool is_error) override;

    Files const& files() const { return files_; }

  private:
    void register_file(FileInfo info);

    void generic_open_entry(pid_t pid, int dirfd, fs::path const& pathname,
                            SyscallState& state);

    void generic_open_exit([[maybe_unused]] pid_t pid, SyscallRet ret_val,
                           bool is_error, SyscallState const& state);

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
        state = FileInfo{.path = path, .size = {}, .existed_before = false};
    }

    void syscall_execve_exit(pid_t /*unused*/, SyscallRet /*unused*/,
                             bool is_error, SyscallState const& state) {
        if (!is_error) {
            if (std::holds_alternative<FileInfo>(state)) {
                auto info = std::get<FileInfo>(state);
                // it succeeded so it has to exist
                info.existed_before = true;

                LOG(DEBUG) << "execve " << info.path;

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

    std::unordered_map<pid_t, PidState> state_;
    Files files_;
    Warnings warnings_;
};

inline void FileTracer::on_syscall_entry(pid_t pid, std::uint64_t syscall,
                                         uint64_t* args) {
    LOG(TRACE) << "syscall_entry: " << syscall << " pid: " << pid;

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

inline void FileTracer::on_syscall_exit(pid_t pid, SyscallRet ret_val,
                                        bool is_error) {
    LOG(TRACE) << "syscall_exit: pid: " << pid;

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

inline void FileTracer::register_file(FileInfo info) {
    std::error_code ec;
    auto& path = info.path;
    if (!path.is_absolute()) {
        path = fs::absolute(path, ec);
        if (ec) {
            LOG(WARN) << "Failed to resolve path to absolute:  " << info.path
                      << ": " << ec.message();
        }
    }

    if (info.existed_before) {
        auto size = fs::file_size(path, ec);

        if (ec) {
            LOG(WARN) << "Failed to get file size of:  " << path << ": "
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

    LOG(DEBUG) << "open " << pathname;

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
                LOG(WARN) << "Failed to resolve dirfd: " << dirfd;
                return;
            }
            result = *d;
        }
        result /= pathname;
    }

    std::error_code ec;
    bool exists = fs::exists(result, ec);
    if (ec) {
        LOG(WARN) << "Failed to check if file exists: " << result << ": "
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
                LOG(WARN) << "Unable to resolve fd: " << ret_val
                          << " to a path";
            } else if (!fs::equivalent(*exit_file, entry_file, ec)) {
                if (ec) {
                    LOG(WARN)
                        << "File entry/exit not-comparable: " << entry_file
                        << " vs " << *exit_file << ": " << ec.message();
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

#endif // FILE_TRACER_H
