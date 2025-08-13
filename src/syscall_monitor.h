#ifndef SYSCALL_MONITOR_H
#define SYSCALL_MONITOR_H

#include "common.h"
#include "logger.h"
#include "process.h"
#include "util.h"
#include "util_io.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <optional>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

using SyscallArgs = std::uint64_t[6];
using SyscallRet = std::int64_t;

class SyscallListener {
  public:
    virtual ~SyscallListener() = default;

    virtual void on_syscall_entry(pid_t pid, std::uint64_t syscall,
                                  SyscallArgs args) = 0;
    virtual void on_syscall_exit(pid_t pid, SyscallRet args, bool is_error) = 0;
};

class SyscallMonitor {
    using ptrace_syscall_info = __ptrace_syscall_info;

  public:
    struct Result {
        enum Kind { Exit, Signal, Failure } kind;
        std::optional<int> detail;
    };

    SyscallMonitor(std::function<int()> tracee, SyscallListener& listener)
        : tracee_{std::move(tracee)}, listener_{listener} {}

    SyscallMonitor(std::vector<std::string> const& cmd,
                   SyscallListener& listener)
        : SyscallMonitor{spawn_process(cmd), listener} {}

    void redirect_stdout(std::ostream& os) { stdout_ = &os; }

    void redirect_stderr(std::ostream& os) { stderr_ = &os; }

    void stop() const;

    Result start();

    [[noreturn]] void process_tracee(Pipe& out, Pipe& err) const;
    Result process_tracer(Pipe& out, Pipe& err);

    static std::string read_string_from_process(pid_t pid, uint64_t remote_addr,
                                                size_t max_len);

  private:
    static int const kSpawnErrorExitCode{254};
    static long const kPtraceOptions{
        // Stop the tracee at the next fork(2) and automatically start
        // tracing the newly forked process
        PTRACE_O_TRACEFORK
        // Stop the tracee at the next vfork(2) and automatically start
        // tracing the newly forked process
        | PTRACE_O_TRACEVFORK
        // Stop the tracee at the next clone(2) and automatically start
        // tracing the newly cloned process
        | PTRACE_O_TRACECLONE
        // Send a SIGKILL signal to the tracee if the tracer exits.
        | PTRACE_O_EXITKILL
        // When delivering system call traps, set bit 7 in
        // the signal number (i.e., deliver SIGTRAP|0x80).
        | PTRACE_O_TRACESYSGOOD};

    static void set_ptrace_options(pid_t pid);

    static void trace_syscalls(pid_t pid);

    void wait_for_initial_stop() const;

    Result monitor();

    void handle_stop(pid_t pid, int status);

    void handle_syscall(pid_t pid);

    static std::function<int()>
    spawn_process(std::vector<std::string> const& cmd);

    std::function<int()> tracee_;
    SyscallListener& listener_;
    std::ostream* stdout_{&std::cout};
    std::ostream* stderr_{&std::cerr};
    pid_t tracee_pid_{-1};
};

inline void SyscallMonitor::stop() const {
    if (tracee_pid_ != -1) {
        kill(tracee_pid_, SIGKILL);
    }
}

inline SyscallMonitor::Result SyscallMonitor::start() {
    Pipe out;
    Pipe err;

    tracee_pid_ = fork();

    if (tracee_pid_ == -1) {
        throw make_system_error(errno, "Error forking the tracee process");
    }

    if (tracee_pid_ != 0) {
        return process_tracer(out, err);
    }
    process_tracee(out, err);
}

inline void SyscallMonitor::process_tracee(Pipe& out, Pipe& err) const {
    if (dup2(out.write(), STDOUT_FILENO) == -1) {
        std::cerr << "dup2 stderr: " << strerror(errno) << " (" << errno
                  << ")\n";
        exit(kSpawnErrorExitCode);
    }

    if (dup2(err.write(), STDERR_FILENO) == -1) {
        std::cerr << "dup2 stderr: " << strerror(errno) << " (" << errno
                  << ")\n";
        exit(kSpawnErrorExitCode);
    }

    out.close();
    err.close();

    if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1) {
        std::cerr << "ptrace: " << strerror(errno) << " (" << errno << ")\n";

        exit(kSpawnErrorExitCode);
    }

    // stop itself and wait until the parent is ready
    raise(SIGSTOP);

    int exit_code = tracee_();
    exit(exit_code);
}

inline SyscallMonitor::Result SyscallMonitor::process_tracer(Pipe& out,
                                                             Pipe& err) {

    out.close_write();
    err.close_write();

    auto stdout_thread_ =
        std::thread([&] { forward_output(out.read(), *stdout_); });

    auto stderr_thread_ =
        std::thread([&] { forward_output(err.read(), *stderr_); });

    wait_for_initial_stop();

    set_ptrace_options(tracee_pid_);

    trace_syscalls(tracee_pid_);

    auto exit_code = monitor();

    // cleanup
    if (stdout_thread_.joinable()) {
        stdout_thread_.join();
    }
    if (stderr_thread_.joinable()) {
        stderr_thread_.join();
    }

    out.close_read();
    err.close_read();

    return exit_code;
}

inline std::string
SyscallMonitor::read_string_from_process(pid_t pid, uint64_t remote_addr,
                                         size_t max_len) {
    static unsigned long page_size = 0;
    if (page_size == 0) {
        unsigned long ps = sysconf(_SC_PAGE_SIZE);
        if (ps <= 0) {
            throw make_system_error(errno, "Failed to get page size");
        }
        page_size = ps;
    }

    if (max_len == 0) {
        return {};
    }

    char* buffer = new char[max_len];

    size_t read_total = 0;
    while (read_total < max_len) {
        // Do not read past page boundary. It might trigger EFAULT while the
        // end of the string might have been already read in the good page.
        // See the note in process_vm_readv(2)
        size_t read_next = max_len > page_size ? page_size : max_len;
        size_t page_offset = (remote_addr + read_next) & (page_size - 1);
        if (read_next > page_offset) {
            read_next -= page_offset;
        }

        auto* local_start = buffer + read_total;
        iovec local_iov{.iov_base = local_start, .iov_len = read_next};

        auto* remote_start = reinterpret_cast<void*>(remote_addr + read_total);
        iovec remote_iov{.iov_base = remote_start, .iov_len = read_next};

        errno = 0;
        ssize_t read = process_vm_readv(pid, &local_iov, 1, &remote_iov, 1, 0);

        if (read < 0) {
            if (errno == EPERM) {
                // TODO: this might be recoverable, using the ptrace
                // peekdata that is however pretty slow as you can read just
                // 8 bytes at a time
                throw make_system_error(errno, "process_vm_readv");
            }

            if (errno == EFAULT) {
                // we can't read further; return what we have so far
                break;
            }

            throw make_system_error(errno, "process_vm_readv");
        }
        read_total += read;

        // we might have some data (read_count >= 0) look for '\0'
        for (char* c = local_start; c < local_start + read; ++c) {
            if (*c == '\0') {
                size_t size = c - buffer;
                return {buffer, size};
            }
        }

        if (static_cast<size_t>(read) != read_next) {
            // could not read more and no '\0' found
            break;
        }
    }

    return {buffer, read_total};
}

inline void SyscallMonitor::trace_syscalls(pid_t pid) {
    if (ptrace(PTRACE_SYSCALL, pid, nullptr, nullptr) == -1) {
        if (errno == ESRCH) {
            // the process has already exited
            return;
        }
        throw make_system_error(
            errno, STR("Failed to start tracing syscalls on pis: " << pid));
    }
}

inline void SyscallMonitor::set_ptrace_options(pid_t pid) {
    if (ptrace(PTRACE_SETOPTIONS, pid, nullptr, (void*)(kPtraceOptions)) ==
        -1) {
        if (errno == ESRCH) {
            // the process has already exited
            return;
        }
        throw make_system_error(
            errno, STR("Failed to set ptrace options on pid: " << pid));
    }
}

inline void SyscallMonitor::wait_for_initial_stop() const {
    using namespace std::chrono_literals;

    auto w = wait_for_signal(tracee_pid_, SIGSTOP, 10ms);
    if (w.status == WaitForSignalResult::Success) {
        return;
    }

    std::string message = "Failed to wait for initial stop: ";
    if (w.status == WaitForSignalResult::Timeout) {
        message += "timeout";
    } else if (w.status == WaitForSignalResult::Exit) {
        message += "child exited with " + std::to_string(*w.detail);
    } else if (w.status == WaitForSignalResult::Signal) {
        message += "child signalled with " + std::to_string(*w.detail);
    }

    throw std::runtime_error(message);
}

inline SyscallMonitor::Result SyscallMonitor::monitor() {
    while (true) {
        int status = 0;
        pid_t wpid = waitpid(-1, &status, __WALL);

        if (wpid < 0) {
            if (errno == ECHILD) {
                // this should never happen as we should exit before
                // when tracee exists
                throw make_system_error(errno,
                                        "waitpid - no more childer to trace");
            }
            if (errno == EINTR) {
                // interrupted by a signal, continue
                continue;
            }

            throw make_system_error(errno, "waitpid");
        }

        if (WIFEXITED(status)) {
            if (wpid == tracee_pid_) {
                auto exit_code = WEXITSTATUS(status);
                if (exit_code == kSpawnErrorExitCode) {
                    return {.kind = Result::Failure, .detail = {}};
                }
                return {.kind = Result::Exit, .detail = WEXITSTATUS(status)};
            }
        } else if (WIFSIGNALED(status)) {
            if (wpid == tracee_pid_) {
                return {.kind = Result::Signal, .detail = WTERMSIG(status)};
            }
        } else if (WIFSTOPPED(status)) {
            handle_stop(wpid, status);
        } else {
            continue;
        }
    }
}

// Handles the stop signal to the tracee or any of its children
// It has to call trace_syscalls on the pid to continue tracing
inline void SyscallMonitor::handle_stop(pid_t pid, int status) {
    unsigned long event = (unsigned long)status >> 16;

    if (event == PTRACE_EVENT_FORK || event == PTRACE_EVENT_CLONE ||
        event == PTRACE_EVENT_VFORK) {

        // new child was created
        // try to set up tracing
        pid_t child_pid = 0;
        if (ptrace(PTRACE_GETEVENTMSG, pid, nullptr, &child_pid) == -1) {
            LOG(WARN) << "Failed to get pid of the new child: "
                      << strerror(errno);
        } else {
            set_ptrace_options(child_pid);
        }

        trace_syscalls(child_pid);
    }

    // 0x80 comes from PTRACE_O_TRACESYSGOOD, so we know that it is a ptrace
    // trap
    if (WSTOPSIG(status) == (SIGTRAP | 0x80)) {
        handle_syscall(pid);
    }

    trace_syscalls(pid);
}

inline void SyscallMonitor::handle_syscall(pid_t pid) {
    ptrace_syscall_info si;
    size_t size = sizeof(si);
    if (ptrace(PTRACE_GET_SYSCALL_INFO, pid, (void*)size, &si) == -1) {
        std::cerr << "Failed to PTRACE_GET_SYSCALL_INFO: " << strerror(errno)
                  << '\n';
    }

    if (si.op == PTRACE_SYSCALL_INFO_ENTRY) {
        listener_.on_syscall_entry(pid, si.entry.nr, si.entry.args);
    } else if (si.op == PTRACE_SYSCALL_INFO_EXIT) {
        listener_.on_syscall_exit(pid, si.exit.rval, si.exit.is_error != 0U);
    }
}

inline std::function<int()>
SyscallMonitor::spawn_process(std::vector<std::string> const& cmd) {
    return [&cmd]() -> int {
        auto c_args = collection_to_c_array(cmd);
        auto const& program = cmd.front();
        execvp(program.c_str(), c_args.data());

        std::cerr << "execvp: " << strerror(errno) << " (" << errno << ") for"
                  << program << "\n";

        return kSpawnErrorExitCode;
    };
}

#endif // SYSCALL_MONITOR_H
