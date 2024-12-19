#include "ptraceMainLoop.hpp"
#include "platformSpecificSyscallHandling.hpp"
#include "ptraceHelpers.hpp"
#include "syscallMapping.hpp"
#include "syscalls/syscallHandlerMapperInline.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>

#include <fcntl.h> //open, AT_FDCWD
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <signal.h> //sigterm handling

namespace {
using namespace frontend;
using namespace std::string_view_literals;

struct ProcessingData {
    std::unordered_map<pid_t, processState> processing;

    processState& getProcesState(pid_t pid, middleend::MiddleEndState& state) {
        auto found = processing.find(pid);
        if (found != processing.end()) {
            return found->second;
        }
        auto emplaced = processing.emplace(pid, pid);
        assert(emplaced.second);
        auto& process = emplaced.first->second;
        // if the PTRACE_O_TRACEEXEC is not set the ptrace will issue a
        // breakpoint after every exec. if it is set, it will instead notify
        // before the exec terminates, or so it seems.
        ptrace(PTRACE_SETOPTIONS, process.pid, nullptr,
               PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK | PTRACE_O_TRACECLONE |
                   PTRACE_O_TRACEEXEC | PTRACE_O_TRACESYSGOOD);

        for (auto& [pid, oldProcess] : processing) {
            // todo: consider keeping the list in a separate structure.
            if (oldProcess.blockedInClone &&
                !oldProcess.blockedInClone->cloneChildPid.has_value()) {
                state.trackNewProcess(
                    process.pid, oldProcess.pid,
                    !(process.blockedInClone->flags & CLONE_FILES),
                    std::nullopt, process.blockedInClone->flags & CLONE_FS);
                oldProcess.blockedInClone->cloneChildPid = process.pid;
                return process;
            }
        }
        state.trackNewProcess(process.pid);
        return process;
    }
    void removeProcessHandling(processState& process) {
        processing.erase(process.pid);
    }
};

struct SignalInfo {
    pid_t pid;

    int status;
    enum state { signal, exit, stop, cont, err } code;

    int signalNr;
    int extendedSignalInfo;
};

bool tryWait(SignalInfo& info) {
    while (true) {
        // tracing all children, they SHOULD be ptraced, but hey, what if they
        // are not? In that case we at least get a ptrace error down the line.
        // I could have kept using the waitid as it turns out. But it gives me
        // no new data and all documentation talks about this... for waitid
        // usage see fields described in
        // https://www.man7.org/linux/man-pages/man2/sigaction.2.html
        pid_t waitStatus = waitpid(-1, &info.status, 0);

        auto waitError = errno;
        if (waitStatus == -1) {
            if (waitStatus == EAGAIN) {
                continue;
            } else if (waitError == ECHILD) {
                return false;
            } else {
                fprintf(stderr, "Wait error %d", waitError);
                return false;
            }
        }
        info.pid = waitStatus;
        info.code = WIFEXITED(info.status)      ? SignalInfo::exit
                    : WIFSIGNALED(info.status)  ? SignalInfo::signal
                    : WIFSTOPPED(info.status)   ? SignalInfo::stop
                    : WIFCONTINUED(info.status) ? SignalInfo::cont
                                                : SignalInfo::err;
        if (info.code == SignalInfo::stop) {
            info.signalNr = WSTOPSIG(info.status);
            info.extendedSignalInfo = info.status >> 8;
        }
        // TODO: add qquick cases for when we just proceed to wait as this is
        // not a relevant handling point.
        break;
    }
    return true;
}

// this si dirty but the handler does not actually take a user-provided
// pointer... if this ever gets threading support, give me guards! I am global!
std::optional<std::function<void()>> registeredHandler;

void sigterm_handler(int signalNr, siginfo_t*, void*) {
    (void)signalNr;
    assert(signalNr == SIGTERM); //
    if (registeredHandler)
        registeredHandler.value()();
}

} // namespace

namespace frontend {

void ptraceChildren(middleend::MiddleEndState& state, bool logSyscalls) {
    // TODO: how about exception handling? unregister in finally?
    struct sigaction oldHandler;
    struct sigaction handler;
    handler.sa_sigaction = sigterm_handler;
    handler.sa_flags = SA_RESETHAND | SA_SIGINFO;
    handler.sa_mask = sigset_t{0};

    auto handlerInstall = sigaction(SIGINT, &handler, &oldHandler);
    if (handlerInstall == -1) {
        fprintf(stderr, "Unable to set sigterm handler\n");
    }
    ProcessingData processesInfo{};
    SignalInfo status;
    bool initialSigtrap = true;
    while (tryWait(status)) {
        registeredHandler = [&]() {
            fprintf(stderr, "SIGINT received. Passing to children. This is not "
                            "a deterministic action, take care.\n");
            for (auto& child : processesInfo.processing) {
                kill(child.first, SIGINT);
            }
        };

        bool doPtrace = true;
        void* signalToPass = 0;
        {
            auto& process = processesInfo.getProcesState(status.pid, state);
            switch (status.code) {
            case SignalInfo::exit:
            case SignalInfo::signal:
                // does not delete as we may have cought the parent exit before
                // the child exec and we need the FD table to be shared
                // properly. Will error on any more syscalls but not error on
                // trace new with the same PID
                state.toBeDeleted(process.pid);
                processesInfo.removeProcessHandling(process);
                doPtrace = false;
                break;
            case SignalInfo::stop: //(traced child has trapped);
                if (status.signalNr != (SIGTRAP | 0x80)) {
                    if (status.signalNr == SIGTRAP &&
                        initialSigtrap) { // manual sigtrap
                        initialSigtrap =
                            false; // we sigtrap once in the entry callback.
                        break;
                    }
                    if (status.extendedSignalInfo ==
                            (SIGTRAP | (PTRACE_EVENT_CLONE << 8)) ||
                        status.extendedSignalInfo ==
                            (SIGTRAP | (PTRACE_EVENT_FORK << 8)) ||
                        status.extendedSignalInfo ==
                            (SIGTRAP | (PTRACE_EVENT_VFORK << 8)) ||
                        status.extendedSignalInfo ==
                            (SIGTRAP | (PTRACE_EVENT_VFORK_DONE << 8)) ||
                        status.extendedSignalInfo ==
                            (SIGTRAP | (PTRACE_EVENT_EXEC << 8))) {
                        break;
                    } else {
                        // this seems to be delivered when a new process spawns.
                        // I have no clue how to catch this properly using
                        // waitid. all non-ptrace signals should just get passed
                        // along.
                        signalToPass = reinterpret_cast<void*>(status.signalNr);
                    }
                }
                /*
                WHY THIS WORKS?

                Syscall-enter-stop and syscall-exit-stop are indistinguishable
                from each other by the tracer. The tracer needs to keep track of
                the sequence of ptrace-stops in order to not misinterpret
                syscall-enter-stop as syscall-exit-stop or vice versa. The rule
                is that syscall-enter-stop is always followed by
                syscall-exit-stop, PTRACE_EVENT stop or the tracee's death; no
                other kinds of ptrace-stop can occur in between.
                */
                else if (process.syscallState == processState::inside) {
                    long val = getSyscallRetval(process.pid);
                    if (process.syscallHandlerObj->operator bool()) {
                        process.syscallHandlerObj->exit(process, state, val);
                        if (logSyscalls)
                            process.syscallHandlerObj->exitLog(process, state,
                                                               val);
                        process.syscallHandlerObj->destroy();
                    }
                    process.syscallState = processState::outside;
                } else if (process.syscallState == processState::outside) {
                    if (getSyscallRetval(process.pid) !=
                        -ENOSYS) { // syscall entry TODO: this may be platform
                                   // specific!!!!
                        assert(false); // i was not a syscall or was not syscall
                                       // entry point.
                    };
                    long syscall_id = getSyscallNr(process.pid);
                    process.syscallHandlerObj->create(syscall_id);
                    process.syscallHandlerObj->entry(process, state,
                                                     syscall_id);
                    if (logSyscalls)
                        process.syscallHandlerObj->entryLog(process, state,
                                                            syscall_id);
                    process.syscallState = processState::inside;
                }
                break;
            case SignalInfo::cont: //(child continued by SIGCONT) - dont care
                break;
            default:
                assert(false);
                break;
            }
        } // the process object may be currently invalid
        if (doPtrace) {
            if (ptrace(PTRACE_SYSCALL, status.pid, nullptr, signalToPass) !=
                0) {
                // TODO: check that this is because all children have quit;
                // the loop itself will terminate due to wait.
            }
        }
    }
    registeredHandler = std::nullopt;
    sigaction(SIGTERM, &oldHandler, nullptr);
}
// intentionally here this way as otherwise the unique_ptr will not compile
processState::processState(pid_t pid)
    : pid(pid),
      syscallHandlerObj(new frontend::SyscallHandlers::HandlerWrapper{}){
          /*
          * TODO: get this value consistently for threads as well.
          pidFD.reset(static_cast<fileDescriptor>(syscall(SYS_pidfd_open, pid,
          0))); //cannot default init as that results in invalid errno auto err
          = errno; if (!pidFD) { //error state fprintf(stderr, "pidfd_open(%d):
          ",pid); switch (err)
                  {
                  case EINVAL:
                          fprintf(stderr, "pid is not valid.(or flags but that
          is impossible) \n");
                          //TODO: this happens when a process is cloned with
          CLONE_THREAD it seems to me. Needs to be researched more. As if only
          the thread group leader could pass its fd over. break; case EMFILE:
                          fprintf(stderr, "The per-process limit on the number
          of open file descriptors has been reached\n"); break; case ENFILE:
                          fprintf(stderr, "The system-wide limit on the total
          number of open fileshas been reached.\n"); break; case ENODEV:
                          fprintf(stderr, "The anonymous inode filesystem is not
          available in this kernel.\n"); break; case ENOMEM: fprintf(stderr,
          "Insufficient kernel memory was available.\n"); break; case ESRCH:
                          fprintf(stderr, "The process specified by pid does not
          exist.\n");
                          //todo: recover?
                          assert(false);
                          break;
                  case ENOSYS:
                          fprintf(stderr, "The kernel you are using does not
          have this syscall.\n"); break; default: fprintf(stderr, "Unknown
          error: %d\n", err); assert(false); //todo; handle unknown error break;
                  }
          }*/

      };
processState::~processState() {}
} // namespace frontend