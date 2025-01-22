#include "backend/backEnd.hpp"
#include "common.hpp"
#include "csv/serialisedFileInfo.hpp"
#include "frontend/ptraceMainLoop.hpp"
#include "logger.hpp"
#include "processSpawnHelper.hpp"
#include "toBeClosedFd.hpp"

#include "./external/argparse.hpp"
#include "util.hpp"

#include <cassert>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <grp.h>
#include <memory>
#include <pwd.h>
#include <string>
#include <sys/ptrace.h>
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
static const int SPAWN_ERROR_EXIT = 254;

struct TraceResult {
    enum Kind { Exit, Signal, Failure } kind;
    std::optional<int> detail;
};

class SyscallTracer {
  public:
    SyscallTracer(fs::path const& program_path,
                  std::vector<std::string> const& args)
        : program_path_{program_path}, args_{args} {}

    void redirect_stdout(std::ostream& os) { stdout_ = &os; }

    void redirect_stderr(std::ostream& os) { stderr_ = &os; }

    void stop() {
        if (child_pid_ != -1) {
            kill(child_pid_, SIGKILL);
        }
    }

    TraceResult run() {
        auto out = util::create_pipe();
        auto err = util::create_pipe();

        child_pid_ = fork();

        if (child_pid_ == -1) {
            close(out.read_fd);
            close(out.write_fd);
            close(err.read_fd);
            close(err.write_fd);

            throw make_system_error(errno, "Error forking the tracee process");
        }

        if (child_pid_ != 0) {
            // TRACER process

            close(out.write_fd);
            close(err.write_fd);

            auto stdout_thread_ = std::thread(
                [&] { forward_output(out.read_fd, *stdout_, "STDOUT"); });

            auto stderr_thread_ = std::thread(
                [&] { forward_output(out.read_fd, *stderr_, "STDERR"); });

            wait_for_initial_stop();

            // configure ptrace options to follow forks/clones
            long options =
                PTRACE_O_TRACEFORK | PTRACE_O_TRACECLONE | PTRACE_O_TRACEVFORK;
            if (ptrace(PTRACE_SETOPTIONS, child_pid_, nullptr, options) == -1) {
                throw std::runtime_error("Failed to set ptrace options: " +
                                         std::string(strerror(errno)));
            }

            // begin tracing syscalls for the tracee
            if (ptrace(PTRACE_SYSCALL, child_pid_, nullptr, nullptr) == -1) {
                throw std::runtime_error("PTRACE_SYSCALL failed: " +
                                         std::string(strerror(errno)));
            }

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

  private:
    void wait_for_initial_stop() {
        using namespace std::chrono_literals;

        auto w = util::wait_for_signal(child_pid_, SIGSTOP, 10ms);
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
                    // this should never happen as we should exit before when
                    // tracee exists
                    throw make_system_error(
                        errno, "waitpid - no more childer to trace");
                }
                if (errno == EINTR) {
                    // interrupted by a signal, continue
                    continue;
                }

                throw make_system_error(errno, "waitpid");
            }

            if (wpid == child_pid_) {
                if (WIFEXITED(status)) {
                    auto exit_code = WEXITSTATUS(status);
                    if (exit_code == SPAWN_ERROR_EXIT) {
                        return {TraceResult::Failure, {}};
                    }
                    return {TraceResult::Exit, WEXITSTATUS(status)};
                }
                if (WIFSIGNALED(status)) {
                    return {TraceResult::Signal, WTERMSIG(status)};
                }
            }

            if (WIFSTOPPED(status)) {
                handle_stop(wpid, status);
            }
        }
    }

    void handle_stop(pid_t pid, int status) {
        // Check for a ptrace event (fork, clone, etc.)
        unsigned long event = (unsigned long)status >> 16;
        if (event == PTRACE_EVENT_FORK || event == PTRACE_EVENT_CLONE ||
            event == PTRACE_EVENT_VFORK) {
            // Child was created; we can retrieve its PID
            long new_child = 0;
            if (ptrace(PTRACE_GETEVENTMSG, pid, nullptr, &new_child) == -1) {
                std::cerr << "Failed to get event message for new child: "
                          << strerror(errno) << std::endl;
            } else {
                // Set the same ptrace options on the new child
                if (ptrace(PTRACE_SETOPTIONS, new_child, nullptr,
                           PTRACE_O_TRACEFORK | PTRACE_O_TRACECLONE |
                               PTRACE_O_TRACEVFORK) == -1) {
                    std::cerr << "Failed to set ptrace options on new child: "
                              << strerror(errno) << std::endl;
                }
                // Let the new child run
                if (ptrace(PTRACE_SYSCALL, new_child, nullptr, nullptr) == -1) {
                    std::cerr
                        << "Failed to continue new child: " << strerror(errno)
                        << std::endl;
                }
            }
        }

        // check if this is a syscall stop
        if (WSTOPSIG(status) == SIGTRAP) {
            struct user_regs_struct regs;
            if (ptrace(PTRACE_GETREGS, pid, nullptr, &regs) == 0) {
                // On x86_64, orig_rax holds the syscall number
                // 'openat' is 257
                constexpr unsigned long SYSCALL_OPENAT = 257;
                if (regs.orig_rax == SYSCALL_OPENAT) {
                    // We are at *entry* to openat or *exit*, depending on the
                    // state Usually, we want the entry to read the path
                    // argument
                    handle_openat(pid, regs);
                }
            } else {
                std::cerr << "Failed to PTRACE_GETREGS: " << strerror(errno)
                          << std::endl;
            }
        }

        // Let the tracee run again, intercepting the next syscall
        if (ptrace(PTRACE_SYSCALL, pid, nullptr, nullptr) == -1) {
            std::cerr << "Failed to PTRACE_SYSCALL pid " << pid << ": "
                      << strerror(errno) << std::endl;
        }
    }

    // Reads the path argument to openat from the tracee’s memory and logs it.
    void handle_openat(pid_t pid, const user_regs_struct& regs) {
        // On x86_64:
        //   rdi = dirfd
        //   rsi = pathname
        //   rdx = flags
        //   r10 = mode
        // We want to read the string at regs.rsi
        uint64_t child_addr = regs.rsi;
        std::string path_str = read_string_from_child(pid, child_addr);
        std::cout << "[Tracer] Process " << pid
                  << " called openat with path: " << path_str << std::endl;
    }

    // Read a null-terminated string from the tracee's memory.
    // Very simplistic approach: read in chunks of sizeof(long) until we hit a
    // null byte or a max length.
    std::string read_string_from_child(pid_t pid, uint64_t addr,
                                       size_t max_length = 4096) {
        std::string result;
        result.reserve(max_length);

        size_t bytes_read = 0;
        while (bytes_read < max_length) {
            errno = 0;
            long data = ptrace(PTRACE_PEEKDATA, pid, (void*)(addr + bytes_read),
                               nullptr);
            if (data == -1 && errno != 0) {
                // Some error
                break;
            }

            // Copy bytes from data until we find '\0' or exhaust this word
            auto bytes = reinterpret_cast<unsigned char*>(&data);
            for (size_t i = 0; i < sizeof(long); i++) {
                if (bytes[i] == '\0') {
                    return result;
                }
                result.push_back(static_cast<char>(bytes[i]));
                bytes_read++;
                if (bytes_read >= max_length) {
                    return result;
                }
            }
        }

        return result;
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
    std::ostream* stdout_{&std::cout};
    std::ostream* stderr_{&std::cerr};
    pid_t child_pid_{-1};
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

    SyscallTracer tracer{program_path, args};

    // Interrupt signals generated in the terminal are delivered to the active
    // process group, which here includes both parent and child. A signal
    // manually generated and sent to an individual process (perhaps with kill)
    // will be delivered only to that process, regardless of whether it is the
    // parent or child.
    // That is why we need to register a signal handler that will terminate the
    // the tracee when the traced gets killed.

    register_signal_handlers([&](int sig) {
        LOG_WARN(log) << "Received signal " << strsignal(sig)
                      << ", stopping the tracing process...";
        tracer.stop();
        exit(1);
    });

    auto result = tracer.run();

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
