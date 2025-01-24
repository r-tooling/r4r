#pragma once

#include "common.hpp"
#include "util.hpp"

#include <cstdint>
#include <cstring>
#include <optional>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using SyscallArgs = std::uint64_t[6];
using SyscallRet = std::int64_t;

class SyscallListener {
  public:
    virtual void on_syscall_entry(pid_t pid, int syscall, SyscallArgs args) = 0;
    virtual void on_syscall_exit(pid_t pid, SyscallRet args, bool is_error) = 0;
};

class SyscallMonitor {
    using ptrace_syscall_info = __ptrace_syscall_info;

  public:
    struct Result {
        enum Kind { Exit, Signal, Failure } kind;
        std::optional<int> detail;
    };

    SyscallMonitor(fs::path const& program_path,
                   std::vector<std::string> const& args,
                   SyscallListener& listener)
        : program_path_{program_path}, args_{args}, listener_{listener} {}

    void redirect_stdout(std::ostream& os) { stdout_ = &os; }

    void redirect_stderr(std::ostream& os) { stderr_ = &os; }

    void stop() {
        if (tracee_pid_ != -1) {
            kill(tracee_pid_, SIGKILL);
        }
    }

    Result start() {
        auto out = util::create_pipe();
        auto err = util::create_pipe();

        tracee_pid_ = fork();

        if (tracee_pid_ == -1) {
            close(out.read_fd);
            close(out.write_fd);
            close(err.read_fd);
            close(err.write_fd);

            throw make_system_error(errno, "Error forking the tracee process");
        }

        if (tracee_pid_ != 0) {
            // TRACER process

            close(out.write_fd);
            close(err.write_fd);

            auto stdout_thread_ = std::thread(
                [&] { forward_output(out.read_fd, *stdout_, "STDOUT"); });

            auto stderr_thread_ = std::thread(
                [&] { forward_output(out.read_fd, *stderr_, "STDERR"); });

            wait_for_initial_stop();
            set_ptrace_option_on_pid(tracee_pid_);
            trace_syscalls(tracee_pid_);

            auto exit_code = monitor();

            // cleanup
            if (stdout_thread_.joinable()) {
                stdout_thread_.join();
            }
            if (stderr_thread_.joinable()) {
                stderr_thread_.join();
            }

            close(out.read_fd);
            close(err.read_fd);

            return exit_code;
        } else {
            // TRACEE process
            if (dup2(out.write_fd, STDOUT_FILENO) == -1) {
                std::cerr << "dup2 stderr: " << strerror(errno) << " (" << errno
                          << ")\n";
                exit(SPAWN_ERROR_EXIT);
            }

            if (dup2(err.write_fd, STDERR_FILENO) == -1) {
                std::cerr << "dup2 stderr: " << strerror(errno) << " (" << errno
                          << ")\n";
                exit(SPAWN_ERROR_EXIT);
            }

            close(out.read_fd);
            close(out.write_fd);
            close(err.read_fd);
            close(err.write_fd);

            if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1) {
                std::cerr << "ptrace: " << strerror(errno) << " (" << errno
                          << ")\n";

                exit(SPAWN_ERROR_EXIT);
            }

            // stop itself and wait until the parent is ready
            raise(SIGSTOP);

            auto c_args = util::collection_to_c_array(args_);
            execvp(program_path_.c_str(), c_args.get());

            std::cerr << "execvp: " << strerror(errno) << " (" << errno
                      << ")\n";

            exit(SPAWN_ERROR_EXIT);
        }
    }

    static std::string read_string_from_process(pid_t pid, long remote_addr,
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

        std::string result(max_len, ' ');

        size_t read_total = 0;
        bool cont = true;
        while (read_total < max_len && cont) {
            // do not read past page boundary
            // see the note in process_vm_readv(2)
            size_t read_next = max_len > page_size ? page_size : max_len;
            size_t page_offset = (remote_addr + read_next) & (page_size - 1);
            if (read_next > page_offset) {
                read_next -= page_offset;
            }

            auto local_start = result.data() + read_total;
            iovec local_iov{.iov_base = local_start, .iov_len = read_next};

            auto remote_start =
                reinterpret_cast<void*>(remote_addr + read_total);
            iovec remote_iov{.iov_base = remote_start, .iov_len = read_next};

            errno = 0;
            ssize_t read =
                process_vm_readv(pid, &local_iov, 1, &remote_iov, 1, 0);

            if (read < 0) {
                if (errno == EPERM) {
                    // TODO: this might be recoverable, using the ptrace
                    // peekdata that is however pretty slow as you can read just
                    // 8 bytes at a time
                    throw make_system_error(errno, "process_vm_readv");
                } else if (errno == EFAULT) {
                    // we can't read further; return what we have so far
                    cont = false;
                } else {
                    throw make_system_error(errno, "process_vm_readv");
                }
            }
            read_total += read;

            // we might have some data (read_count >= 0)
            // look for '\0' in the chunk
            for (char* c = local_start; c < local_start + read; ++c) {
                if (*c == '\0') {
                    size_t new_size = c - result.data();
                    result.resize(new_size);
                    return result;
                }
            }

            if (static_cast<size_t>(read) != read_next) {
                // could not read more
                // and no '\0' found
                break;
            }
        }

        result.resize(read_total);
        return result;
    }

  private:
    static const int SPAWN_ERROR_EXIT = 254;

    void set_ptrace_option_on_pid(pid_t pid) {
        static long options =
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
            | PTRACE_O_TRACESYSGOOD;

        if (ptrace(PTRACE_SETOPTIONS, pid, nullptr, (void*)(options)) == -1) {
            throw make_system_error(errno, "Failed to set ptrace options on " +
                                               std::to_string(pid));
        }
    }

    void trace_syscalls(pid_t pid) {
        if (ptrace(PTRACE_SYSCALL, pid, nullptr, nullptr) == -1) {
            throw make_system_error(errno,
                                    "Failed to start tracing syscalls on " +
                                        std::to_string(pid));
        }
    }

    void wait_for_initial_stop() {
        using namespace std::chrono_literals;

        auto w = util::wait_for_signal(tracee_pid_, SIGSTOP, 10ms);
        if (w.status == util::WaitForSignalResult::Success) {
            return;
        }

        std::string message = "Failed to wait for initial stop: ";
        if (w.status == util::WaitForSignalResult::Timeout) {
            message += "timeout";
        } else if (w.status == util::WaitForSignalResult::Exit) {
            message += "child exited with " + std::to_string(*w.detail);
        } else if (w.status == util::WaitForSignalResult::Signal) {
            message += "child signalled with " + std::to_string(*w.detail);
        }

        throw std::runtime_error(message);
    }

    Result monitor() {
        while (true) {
            int status = 0;
            pid_t wpid = waitpid(-1, &status, __WALL);

            if (wpid < 0) {
                if (errno == ECHILD) {
                    // this should never happen as we should exit before
                    // when tracee exists
                    throw make_system_error(
                        errno, "waitpid - no more childer to trace");
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
                    if (exit_code == SPAWN_ERROR_EXIT) {
                        return {Result::Failure, {}};
                    }
                    return {Result::Exit, WEXITSTATUS(status)};
                }
            } else if (WIFSIGNALED(status)) {
                if (wpid == tracee_pid_) {
                    return {Result::Signal, WTERMSIG(status)};
                }
            } else if (WIFSTOPPED(status)) {
                handle_stop(wpid, status);
            } else {
                continue;
            }
        }
    }

    void handle_stop(pid_t pid, int status) {
        unsigned long event = (unsigned long)status >> 16;
        if (event == PTRACE_EVENT_FORK || event == PTRACE_EVENT_CLONE ||
            event == PTRACE_EVENT_VFORK) {
            // new child was created
            unsigned long child_pid = 0;
            if (ptrace(PTRACE_GETEVENTMSG, pid, nullptr, &child_pid) == -1) {
                // FIXME: logging
                std::cerr << "Failed to get event message for new child: "
                          << strerror(errno) << std::endl;
            } else {
                set_ptrace_option_on_pid(child_pid);
                trace_syscalls(child_pid);
            }
        }

        // 0x80 comes from PTRACE_O_TRACESYSGOOD so we know that it is a ptrace
        // trap
        if (WSTOPSIG(status) == (SIGTRAP | 0x80)) {
            handle_syscall(pid);
        }

        trace_syscalls(pid);
    }

    void handle_syscall(pid_t pid) {
        ptrace_syscall_info si;
        size_t size = sizeof(si);
        if (ptrace(PTRACE_GET_SYSCALL_INFO, pid, (void*)size, &si) == -1) {
            std::cerr << "Failed to PTRACE_GET_SYSCALL_INFO: "
                      << strerror(errno) << std::endl;
        }

        if (si.op == PTRACE_SYSCALL_INFO_ENTRY) {
            listener_.on_syscall_entry(pid, si.entry.nr, si.entry.args);
        } else if (si.op == PTRACE_SYSCALL_INFO_EXIT) {
            listener_.on_syscall_exit(pid, si.exit.rval, si.exit.is_error);
        }
    }

    static void forward_output(int read_fd, std::ostream& os, const char* tag) {
        constexpr size_t BUFFER_SIZE = 1024;
        std::array<char, BUFFER_SIZE> buffer{};
        while (true) {
            ssize_t bytes = ::read(read_fd, buffer.data(), buffer.size());
            if (bytes == 0) {
                break; // EOF
            } else if (bytes < 0) {
                if (errno == EINTR) {
                    continue; // retry
                }
                std::cerr << "[Tracer] " << tag
                          << " read error: " << strerror(errno) << std::endl;
                break;
            }
            os.write(buffer.data(), bytes);
            os.flush();
        }
    }

    const fs::path program_path_;
    const std::vector<std::string> args_;
    SyscallListener& listener_;
    std::ostream* stdout_{&std::cout};
    std::ostream* stderr_{&std::cerr};
    pid_t tracee_pid_{-1};
};
