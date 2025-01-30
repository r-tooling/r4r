#include "pipe.hpp"
namespace frontend::SyscallHandlers {
void Pipe::entry(processState& process, const middleend::MiddleEndState&,
                 long) {
    pipesPtr = process.getSyscallParam<1>();
    // todo: do we ever care about O_NONBLOCK and O_CLOEXEC
}

void Pipe::exit(processState& process, middleend::MiddleEndState& state,
                long syscallRetval) {
    if (syscallRetval == 0) {
        auto pipes = userPtrToOwnPtr<fileDescriptor, 2>(process.pid, pipesPtr);
        state.registerPipe(process.pid, pipes.get());
    }
}

void Pipe::entryLog(const processState& process,
                    const middleend::MiddleEndState& state, long syscallNr) {
    simpleSyscallHandler_base::entryLog(process, state, syscallNr);
}
} // namespace frontend::SyscallHandlers