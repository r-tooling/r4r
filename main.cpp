#include "backend/backEnd.hpp"
#include "common.hpp"
#include "csv/serialisedFileInfo.hpp"
#include "logger.hpp"
#include <filesystem>
#include <grp.h>

#include "./external/argparse.hpp"
#include "syscall_monitor.hpp"
#include "util.hpp"

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <pwd.h>
#include <stdexcept>
#include <string>

#include <cerrno>

#include <unordered_map>
#include <vector>

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

// refactoring start

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

struct File {
    fs::path path;
    bool existed_before;
};

class FileTracer : public SyscallListener {
    using SyscallState = std::uint64_t;
    using PidState = std::pair<int, SyscallState>;

    struct SyscallHandler {
        void (FileTracer::*entry)(pid_t, SyscallArgs, SyscallState*);
        void (FileTracer::*exit)(pid_t, SyscallRet, bool, SyscallState*);
    };

  public:
    FileTracer() {}

    void on_syscall_entry(pid_t pid, std::uint64_t syscall, SyscallArgs args) override {
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

    void on_syscall_exit(pid_t pid, SyscallRet retval, bool is_error) override {
        auto node = state_.extract(pid);
        if (node) {
            auto [syscall, state] = node.mapped();
            auto it = kHandlers_.find(syscall);
            if (it == kHandlers_.end()) {
                throw std::runtime_error(
                    STR("No exit handler for syscall: " << syscall));
            }
            auto handler = it->second;
            (this->*(handler.exit))(pid, retval, is_error, &state);
        }
    }

  private:
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
                    // TODO: log warning
                    std::cerr << "Failed to resolve cwd of: " << pid
                              << std::endl;
                    return;
                }
                result = *d;
            } else {
                // TODO: log warning
                auto d = util::resolve_fd_filename(pid, dirfd);
                if (!d) {
                    std::cerr << "Failed to resolve dirfd: " << dirfd
                              << std::endl;
                    return;
                }
                result = *d;
            }
            result /= pathname;
        }

        std::cout << "openat: " << pathname << std::endl;
        File* file = new File{result, fs::exists(result)};
        *state = reinterpret_cast<SyscallState>(file);
    }

    void syscall_openat_exit([[maybe_unused]] pid_t pid, SyscallRet retval,
                             bool is_error, SyscallState* state) {
        (void)pid;

        if (is_error) {
            return;
        }

        auto file = reinterpret_cast<File*>(*state);
        // TODO: register the file
        if (file) {
            if (retval >= 0) {
                std::cout << "openat: " << file->path << " => " << retval
                          << std::endl;
            }
            delete file;
        }
    }

    void register_fd(pid_t pid, int fd) {
        auto path = util::resolve_fd_filename(pid, fd);
        if (!path) {
            // FIXME: logging
            std::cout << "Unable to registered fd: " << fd << std::endl;
            return;
        }

        std::cout << "Registered fd: " << fd << ": " << *path << std::endl;
    }

    static const inline std::unordered_map<int, SyscallHandler> kHandlers_{
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
    SyscallMonitor monitor{program_path, args, tracer};

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
    case SyscallMonitor::Result::Failure:
        LOG_ERROR(log) << "Failed to spawn the process";
        return 1;

    case SyscallMonitor::Result::Signal:
        LOG_ERROR(log) << "Program was termined by signal: " << *result.detail;
        return 1;

    case SyscallMonitor::Result::Exit:
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
