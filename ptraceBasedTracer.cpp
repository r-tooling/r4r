#include "ptraceBasedTracer.hpp"
#include "backend/backEnd.hpp"
#include "csv/serialisedFileInfo.hpp"
#include "frontend/ptraceMainLoop.hpp"
#include "processSpawnHelper.hpp"
#include "toBeClosedFd.hpp"

#include "./external/argparse.hpp"

#include <cassert>

#include <cstddef>
#include <grp.h>
#include <pwd.h>
#include <string>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

#include <fcntl.h> //open, close...
#include <unordered_map>

using std::size_t;

#include <sys/wait.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif // !_GNU_SOURCE

#include <sched.h>

namespace {

void fileOpenFail(int err) noexcept {
    switch (err) {
    case EACCES:
        fprintf(stderr,
                "| The requested access to the file is not allowed, or search "
                "permission is denied for one of the directories in the"
                "path prefix of pathname, or the file did not exist yet and "
                "write access to the parent directory is not allowed. \n"
                "| Where O_CREAT is specified, the protected_fifos or "
                "protected_regular sysctl is enabled, the file already "
                "exists and is a FIFO or regular file, the owner of the "
                "file is neither the current user nor the owner of the "
                "containing directory, and the containing directory is both "
                "world - or group - writable and sticky.\n");
        break;
    default:
        fprintf(stderr, "unknown error %d", err);
        break;
    }
}
} // namespace

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

int main(int argc, char* argv[]) {
    argparse::ArgumentParser program{"", "", argparse::default_arguments::help};
    program.add_description("The tracer is used for analysing the dependencies "
                            "of other computer programs.");

    std::vector<std::string> args;

    auto& group = program.add_mutually_exclusive_group(true);
    group.add_argument("--analyse")
        .help("only run the final data analysis")
        .nargs(0, 0);
    group.add_argument("--run")
        .help("only run the initial program without analysis run analysis. "
              "Signifies and end of arguments")
        .metavar(" ")
        .remaining()
        .nargs(argparse::nargs_pattern::at_least_one)
        .store_into(args);
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

    if (args.empty() && !program.is_used("--analyse")) {
        std::cout << program;
        return 255;
    }
    if (!program.is_used("--analyse")) {
        std::filesystem::path programName{args.front()};

        auto Tofree = FreeUniquePtr{get_current_dir_name()};

        middleend::MiddleEndState state{Tofree.get(), environ, args};
        Tofree.reset();

        args.erase(args.begin());

        std::cout << "Running " << programName << " with " << args.size()
                  << " arguments ";
        for (auto& item : args) {
            std::cout << item << " ";
        }
        std::cout << std::endl;

        ///  TRACING
        {
            int in = fileno(stdin);
            int out = fileno(stdout);
            int err = fileno(stderr);
            (void)in;
            (void)out;
            (void)err;
            assert(in >= 0 && in < 3); // should be true anywhere.
            assert(err >= 0 && err < 3);
            assert(out >= 0 && out < 3);
        }

        /*
                redirect the ran program outputs to predefined files.
        */

        ToBeClosedFd outPipe{
            open("stdout.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IWUSR)};
        auto err = errno;
        if (outPipe.get() == -1) {
            fileOpenFail(err);
            return -1;
        }
        ToBeClosedFd errPipe{
            open("stderr.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IWUSR)};
        err = errno;
        if (errPipe.get() == -1) {
            fileOpenFail(errno);
            return -1;
        }
        auto inPipe = writeClosedPipe();

        auto callback = []() noexcept {
            while (ptrace(PTRACE_TRACEME, 0, 0, 0) != -1)
                ; // wait utill parent is ready.  On error, all requests return
                  // -1. This should error out when parent is already attatched.
            raise(SIGTRAP);
        };

        pid_t childPid = spawnProcessWithSimpleArgs(
            inPipe.get(), outPipe.get(), errPipe.get(), programName,
            ArgvWrapper{args},
            callback); // arg 1 == my filename, arg 2 == his filename
        // will recieve sigchild when it terminates and thus we can safely free
        // the stack when taht happens. Though that should really be only done
        // at the termination of main as otherwise we could get confused by
        // getting sigchaild called from any other source.
        //  so not waiting for this PID is intentional.
        (void)childPid;
        frontend::ptraceChildren(state);
        wait(nullptr);
        printf("Child process terminated! Analysing data.\n");

        CSV::serializeFiles(state.encounteredFilenames, "rawFiles.csv");
        CSV::serializeEnv(state.env, "env.csv");
        CSV::serializeEnv(state.args, "args.csv");
        CSV::serializeWorkdir(state.initialDir, "workdir.txt");
    }
    if (!program.is_used("--run")) {
        // ANALYSIS
        LoadAndAnalyse();
    }
    return 0;
}
