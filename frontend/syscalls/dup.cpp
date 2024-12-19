#include "dup.hpp"
namespace frontend::SyscallHandlers {

void Dup23::entry(processState& process, const middleend::MiddleEndState&,
                  long) {
    oldFd = process.getSyscallParam<1>();
    newFd = process.getSyscallParam<2>();
    // todo: do we care about O_CLOEXEC?
}

void Dup23::exit(processState& process, middleend::MiddleEndState& state,
                 long syscallRetval) {
    if (syscallRetval >= 0) {
        state.registerFdAlias(process.pid, newFd, oldFd);
    }
}

void Dup23::entryLog(const processState& process,
                     const middleend::MiddleEndState& state, long syscallNr) {
    simpleSyscallHandler_base::entryLog(process, state, syscallNr);
    strBuf << "(" << newFd << " = ";
    appendResolvedFilename<false>(process, state, oldFd, strBuf);
    strBuf << ")";
}

void Dup::entry(processState& process, const middleend::MiddleEndState&, long) {
    oldFd = process.getSyscallParam<1>();
}

void Dup::exit(processState& process, middleend::MiddleEndState& state,
               long syscallRetval) {
    if (syscallRetval >= 0) {
        state.registerFdAlias(process.pid, syscallRetval, oldFd);
    }
}

void Dup::entryLog(const processState& process,
                   const middleend::MiddleEndState& state, long syscallNr) {
    simpleSyscallHandler_base::entryLog(process, state, syscallNr);
    strBuf << "(retval = ";
    appendResolvedFilename(process, state, oldFd, strBuf);
    strBuf << ")";
}
} // namespace frontend::SyscallHandlers