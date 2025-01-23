#include "backend/backEnd.hpp"
#include "common.hpp"
#include "csv/serialisedFileInfo.hpp"
#include "frontend/ptraceMainLoop.hpp"
#include "logger.hpp"
#include "processSpawnHelper.hpp"
#include "toBeClosedFd.hpp"

#include "./external/argparse.hpp"
#include "util.hpp"

#include <asm/unistd_64.h>
#include <cassert>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <grp.h>
#include <iostream>
#include <linux/ptrace.h>
#include <memory>
#include <pwd.h>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

#include <fcntl.h> //open, close...
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

using std::size_t;

#include <sys/wait.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif // !_GNU_SOURCE

#include <sched.h>

backend::UserInfo get_user_info() {

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

    // Retrieve primary group information
    group* grp = getgrgid(gid);
    if (!grp) {
        throw std::runtime_error("Failed to get group struct for GID " +
                                 std::to_string(gid));
    }
    backend::GroupInfo primary_group = {gid, grp->gr_name};

    // Get the list of groups
    int ngroups = 0;
    getgrouplist(username.c_str(), gid, nullptr,
                 &ngroups); // Get number of groups

    std::vector<gid_t> group_ids(ngroups);
    if (getgrouplist(username.c_str(), gid, group_ids.data(), &ngroups) == -1) {
        throw std::runtime_error("Failed to get group list for user " +
                                 username);
    }

    // Map group IDs to GroupInfo
    std::vector<backend::GroupInfo> groups;
    for (gid_t group_id : group_ids) {
        group* grp = getgrgid(group_id);
        if (grp) {
            groups.push_back({group_id, grp->gr_name});
        }
    }

    return {uid, primary_group, username, home_directory, shell, groups};
}

void do_analysis(
    std::unordered_map<absFilePath, middleend::file_info> const& fileInfos,
    std::vector<std::string> const& envs, std::vector<std::string> const& cmd,
    fs::path const& work_dir) {

    std::vector<middleend::file_info> files;
    for (const auto& [_, file] : fileInfos) {
        files.push_back(file);
    }

    std::unordered_map<std::string, std::string> env;
    for (const auto& e : envs) {
        auto pos = e.find('=');
        if (pos != std::string::npos) {
            env[e.substr(0, pos)] = e.substr(pos + 1);
        } else {
            // FIXME: logging
            std::cerr << "Invalid env variable: " << e << ": missing `=`"
                      << std::endl;
        }
    }

    auto user = get_user_info();
    backend::Trace trace{files, env, cmd, work_dir, user};
    backend::DockerfileTraceInterpreter interpreter{trace};

    interpreter.finalize();
}

void LoadAndAnalyse() {
    auto fileInfos = CSV::deSerializeFiles("rawFiles.csv");
    auto origEnv = CSV::deSerializeEnv("env.csv");
    auto origArgs = CSV::deSerializeEnv("args.csv");
    auto origWrkdir = CSV::deSerializeWorkdir("workdir.txt");
    do_analysis(fileInfos, origEnv, origArgs, origWrkdir);
}

// refactring start

struct TraceResult {
    enum Kind { Exit, Signal, Failure } kind;
    std::optional<int> detail;
};

class SyscallMonitor {
    using Callback = std::function<void(pid_t, ptrace_syscall_info&)>;

  public:
    SyscallMonitor(fs::path const& program_path,
                   std::vector<std::string> const& args, Callback callback)
        : program_path_{program_path}, args_{args}, callback_{callback} {}

    void redirect_stdout(std::ostream& os) { stdout_ = &os; }

    void redirect_stderr(std::ostream& os) { stderr_ = &os; }

    void stop() {
        if (tracee_pid_ != -1) {
            kill(tracee_pid_, SIGKILL);
        }
    }

    TraceResult start() {
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

            if (do_ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1) {
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
                                                size_t len) {
        // If max_length == 0, there's nothing to read
        if (len == 0) {
            return {};
        }

        static unsigned long page_size = 0;
        if (page_size == 0) {
            unsigned long ps = sysconf(_SC_PAGE_SIZE);
            if (ps <= 0) {
                throw make_system_error(errno, "Failed to get page size");
            }
            page_size = ps;
        }

        std::string result(len, ' ');

        size_t total_read = 0;
        bool cont = true;
        while (total_read < len && cont) {
            // do not read past page boundary
            // see the note in process_vm_readv(2)
            size_t want_to_read = len > page_size ? page_size : len;
            size_t page_offset = (remote_addr + want_to_read) & (page_size - 1);
            if (want_to_read > page_offset) {
                want_to_read -= page_offset;
            }

            char* local_start = result.data() + total_read;
            struct iovec local_iov {
                .iov_base = local_start, .iov_len = want_to_read
            };
            struct iovec remote_iov {
                .iov_base = reinterpret_cast<void*>(remote_addr + total_read),
                .iov_len = want_to_read
            };

            errno = 0;
            ssize_t read =
                process_vm_readv(pid, &local_iov, 1, &remote_iov, 1, 0);

            if (read < 0) {
                if (errno == EPERM) {
                    throw make_system_error(errno, "process_vm_readv");
                    // return read_string_from_process_ptrace(pid, remote_addr,
                    // len);
                } else if (errno == EFAULT) {
                    // we can't read further; return what we have so far
                    cont = false;
                } else {
                    throw make_system_error(errno, "process_vm_readv");
                }
            }
            total_read += read;

            // we might have some data (read_count >= 0)
            // look for '\0' in the chunk
            for (char* c = local_start; c < local_start + read; ++c) {
                if (*c == '\0') {
                    size_t new_size = c - result.data();
                    result.resize(new_size);
                    return result;
                }
            }

            if (static_cast<size_t>(read) != want_to_read) {
                // could not read more
                // and no '\0' found
                break;
            }
        }

        result.resize(total_read);
        return result;
    }

  private:
    static const int SPAWN_ERROR_EXIT = 254;

    static long do_ptrace(int request, pid_t pid, void* addr, void* data) {
        return ptrace(static_cast<__ptrace_request>(request), pid, addr, data);
    }

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

        if (do_ptrace(PTRACE_SETOPTIONS, pid, nullptr, (void*)(options)) ==
            -1) {
            throw make_system_error(errno, "Failed to set ptrace options on " +
                                               std::to_string(pid));
        }
    }

    void trace_syscalls(pid_t pid) {
        if (do_ptrace(PTRACE_SYSCALL, pid, nullptr, nullptr) == -1) {
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

    TraceResult monitor() {
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
                        return {TraceResult::Failure, {}};
                    }
                    return {TraceResult::Exit, WEXITSTATUS(status)};
                }
            } else if (WIFSIGNALED(status)) {
                if (wpid == tracee_pid_) {
                    return {TraceResult::Signal, WTERMSIG(status)};
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
            if (do_ptrace(PTRACE_GETEVENTMSG, pid, nullptr, &child_pid) == -1) {
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
        struct ptrace_syscall_info si;
        size_t size = sizeof(si);
        if (do_ptrace(PTRACE_GET_SYSCALL_INFO, pid, (void*)size, &si) == -1) {
            std::cerr << "Failed to PTRACE_GET_SYSCALL_INFO: "
                      << strerror(errno) << std::endl;
        }

        callback_(pid, si);
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
    Callback callback_;
    std::ostream* stdout_{&std::cout};
    std::ostream* stderr_{&std::cerr};
    pid_t tracee_pid_{-1};
};

std::function<void(int)> global_signal_handler;

void register_signal_handlers(std::function<void(int)> handler) {
    global_signal_handler = handler;
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

using SyscallArgs = unsigned long long[6];
using SyscallRet = long long;

struct File {
    fs::path path;
    bool existed_before;
};

class FileTracer {
    using SyscallState = std::uint64_t;
    using PidState = std::pair<int, SyscallState>;

  public:
    void trace_syscall_entry(pid_t pid, int syscall_nr, SyscallArgs args) {
        SyscallState state = 0;

        if (do_entry(pid, syscall_nr, args, &state)) {
            auto [s_it, inserted] = state_.try_emplace(pid, syscall_nr, state);
            if (!inserted) {
                throw std::runtime_error(
                    STR("There is already a syscall handler for pid: " << pid));
            }
        }
    }

    void trace_syscall_exit(pid_t pid, SyscallRet retval, bool is_error) {
        auto node = state_.extract(pid);
        if (node) {
            auto [syscall_nr, state] = node.mapped();
            do_exit(pid, syscall_nr, retval, is_error, &state);
        }
    }

  private:
    bool do_entry(pid_t pid, int syscall_nr, SyscallArgs args,
                  SyscallState* state) {
        (void)pid;
        (void)args;
        (void)state;

        switch (syscall_nr) {
        case __NR_openat: {
            auto arg1 = SyscallMonitor::read_string_from_process(pid, args[1],
                                                                 PATH_MAX);

            std::cout << "openat: " << arg1 << std::endl;

            // auto path = resolve_path(arg1);
            // File* file = new File;
            return true;
        }
        default:
            return false;
        }
    }

    void do_exit(pid_t pid, int syscall_nr, SyscallRet retval, bool is_error,
                 SyscallState* state) {
        (void)pid;
        (void)syscall_nr;
        (void)retval;
        (void)is_error;
        (void)state;

        switch (syscall_nr) {
        case __NR_openat:
            if (retval >= 0) {
                register_fd(pid, static_cast<int>(retval));
            }
            return;
        default:
            return;
        }
    }

    void register_fd(pid_t pid, int fd) {
        auto path = resolve_fd_filename(pid, fd);
        if (!path) {
            // FIXME: logging
            std::cout << "Unable to registered fd: " << fd << std::endl;
            return;
        }

        std::cout << "Registered fd: " << fd << ": " << *path << std::endl;
    }

    static std::optional<fs::path> resolve_fd_filename(pid_t pid, int fd) {
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

    std::unordered_map<pid_t, PidState> state_;
};

int main(int argc, char* argv[]) {
    Logger log{"main", "{elapsed_time} {level} {message}"};
    log.add_sink(std::make_shared<std::ostream>(std::cout.rdbuf()));

    argparse::ArgumentParser program{"", "", argparse::default_arguments::help};
    program.add_description("The tracer is used for analysing the dependencies "
                            "of other computer programs.");

    std::vector<std::string> args;

    auto& group = program.add_mutually_exclusive_group(true);
    group.add_argument("--")
        .help("used to signify end of arguments")
        .metavar(" ")
        .remaining()
        .nargs(argparse::nargs_pattern::at_least_one)
        .store_into(args);
    group.add_argument("arguments")
        .help("Subprogram name and arguments")
        .metavar("<subprogram arguments>")
        .remaining()
        .nargs(argparse::nargs_pattern::any)
        .store_into(args);

    program.set_usage_break_on_mutex();

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << std::endl;
        std::cerr << program;
        return 1;
    }

    std::filesystem::path program_path{args.front()};
    std::filesystem::path cwd{std::filesystem::current_path()};

    LOG_INFO(log) << "Running: " << util::mk_string(args, ' ') << " in " << cwd;

    FileTracer tracer;
    SyscallMonitor monitor{
        program_path, args, [&](pid_t pid, ptrace_syscall_info& si) {
            if (si.op == PTRACE_SYSCALL_INFO_ENTRY) {
                tracer.trace_syscall_entry(pid, si.entry.nr, si.entry.args);
            } else if (si.op == PTRACE_SYSCALL_INFO_EXIT) {
                tracer.trace_syscall_exit(pid, si.exit.rval, si.exit.is_error);
            }
        }};

    // Interrupt signals generated in the terminal are delivered to the
    // active process group, which here includes both parent and child. A
    // signal manually generated and sent to an individual process (perhaps
    // with kill) will be delivered only to that process, regardless of
    // whether it is the parent or child. That is why we need to register a
    // signal handler that will terminate the the tracee when the tracer
    // gets killed.

    register_signal_handlers([&](int sig) {
        LOG_WARN(log) << "Received signal " << strsignal(sig)
                      << ", stopping the tracing process...";
        monitor.stop();
        exit(1);
    });

    auto result = monitor.start();

    switch (result.kind) {
    case TraceResult::Failure:
        LOG_ERROR(log) << "Failed to spawn the process";
        return 1;

    case TraceResult::Signal:
        LOG_ERROR(log) << "Program was termined by signal: " << *result.detail;
        return 1;

    case TraceResult::Exit:
        int exit_code = *result.detail;
        if (exit_code != 0) {
            LOG_ERROR(log) << "Program exited with: " << exit_code;
            return 1;
        }
        LOG_INFO(log) << "Program exited successfully, analyzing the results";
    }

    // // FIXME: refactor
    // CSV::serializeFiles(state.encounteredFilenames, "rawFiles.csv");
    // CSV::serializeEnv(state.env, "env.csv");
    // CSV::serializeEnv(state.args, "args.csv");
    // CSV::serializeWorkdir(state.initialDir, "workdir.txt");
    //
    // // ANALYSIS
    // LoadAndAnalyse();

    return 0;
}
