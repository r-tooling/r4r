#include "fnctl.hpp"
namespace frontend::SyscallHandlers {

void Fcntl::entry(processState& process, const middleend::MiddleEndState&,
                  long) {
    oldFd = process.getSyscallParam<1>();
    auto command = process.getSyscallParam<2>();
    syscallHandling = nullopt;

    switch (command) {
    case F_DUPFD:
    case F_DUPFD_CLOEXEC: {
        syscallHandling = dup;
        break;
    }
    /*     The following commands manipulate the flags associated with a
            file descriptor.Currently, only one such flag is defined :
            FD_CLOEXEC close-on-exec*/
    case F_GETFD:
    case F_SETFD:
        break;
        /*On Linux, this command can change
                only the O_APPEND, O_ASYNC, O_DIRECT, O_NOATIME, and
                O_NONBLOCK flags.It is not possible to change the
                O_DSYNC and O_SYNC flags; see BUGS, below. - none of these seem
           to matter to us*/
    case F_SETFL:
    case F_GETFL:
        break;
    case F_SETLK: // file locking modes. Can be used for communication with
                  // other processes, could be used for heuristics regarding
                  // this.
    case F_SETLKW:
    case F_GETLK:
    case F_OFD_SETLK:
    case F_OFD_SETLKW:
    case F_OFD_GETLK:
        break;
    case F_GETOWN:
    case F_SETOWN:
    case F_GETOWN_EX:
    case F_SETOWN_EX:
    case F_GETSIG:
        break;
    case F_SETLEASE: // file ownership knowledge
    case F_GETLEASE:
        break;
    case F_NOTIFY: // basically inode wathing
        break;
    case F_SETPIPE_SZ: // pipe capacity changes. Irrelevant for our usecase
    case F_GETPIPE_SZ:
        break;
    case F_ADD_SEALS: // relevant for
                      // https://www.man7.org/linux/man-pages/man2/memfd_create.2.html
    case F_GET_SEALS:
        break;
    case F_GET_RW_HINT: // don't care, only optimisations'
    case F_SET_RW_HINT:
    case F_GET_FILE_RW_HINT:
    case F_SET_FILE_RW_HINT:
        break;
    }

    // todo: do we care about O_CLOEXEC?
}

void Fcntl::exit(processState& process, middleend::MiddleEndState& state,
                 long syscallRetval) {
    if (syscallHandling == dup) { // todo: reuse dup impl
        if (syscallRetval >= 0) {
            state.getFilePath<true>(process.pid, oldFd);
            state.registerFdAlias(process.pid, syscallRetval, oldFd);
        }
    } else {
        assert(syscallHandling == nullopt);
    }
}

void Fcntl::entryLog(const processState& process,
                     const middleend::MiddleEndState& state, long syscallNr) {
    simpleSyscallHandler_base::entryLog(process, state, syscallNr);
    if (syscallHandling == dup) {
        strBuf << "( retval = ";
    } else {
        strBuf << "( operation on ";
    }
    appendResolvedFilename<false>(process, state, oldFd, strBuf);
    strBuf << ")";
}
} // namespace frontend::SyscallHandlers