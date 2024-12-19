#include "ptraceBasedTracer.hpp"
#include "backend/backEnd.hpp"
#include "csv/serialisedFileInfo.hpp"
#include "frontend/ptraceMainLoop.hpp"
#include "processSpawnHelper.hpp"
#include "toBeClosedFd.hpp"

#include "./external/argparse.hpp"

#include <cassert>

#include <cstddef>
#include <sys/ptrace.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

#include <fcntl.h> //open, close...

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

void doAnalysis(
    std::unordered_map<absFilePath,
                       std::unique_ptr<middleend::MiddleEndState::file_info>>&
        fileInfos,
    std::vector<std::string>& origEnv, std::vector<std::string>& origArgs,
    std::filesystem::__cxx11::path& origWrkdir) {

    backend::CachingResolver backendResolver{fileInfos, origEnv, origArgs,
                                             origWrkdir};

    printf("Analysing R packages\n");
    backendResolver.resolveRPackages();
    printf("Analysing Debian packages\n");
    backendResolver.resolveDebianPackages();
    printf("Creating reports\n");
    backendResolver.csv("accessedFiles.csv");
    backendResolver.report("report.txt");
    backendResolver.dockerImage(".", "r4r:test");
    printf("Done\n");
}

void LoadAndAnalyse() {
    auto fileInfos = CSV::deSerializeFiles("rawFiles.csv");
    auto origEnv = CSV::deSerializeEnv("env.csv");
    auto origArgs = CSV::deSerializeEnv("args.csv");
    auto origWrkdir = CSV::deSerializeWorkdir("workdir.txt");
    doAnalysis(fileInfos, origEnv, origArgs, origWrkdir);
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
