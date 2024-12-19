#include "read.hpp"
namespace frontend::SyscallHandlers {
void FileOperationLogger::entry(processState& process,
                                const middleend::MiddleEndState&, long) {
    fd = process.getSyscallParam<1>();
}

void FileOperationLogger::exit(processState& process,
                               middleend::MiddleEndState& state,
                               long syscallRetval) {
    // we just log this. no "real operations happening
    if (syscallRetval >= 0) {
        state.getFilePath<true>(process.pid, fd);
    }
}

void FileOperationLogger::entryLog(const processState& process,
                                   const middleend::MiddleEndState& state,
                                   long syscallNr) {
    simpleSyscallHandler_base::entryLog(process, state, syscallNr);
    strBuf << "(";
    appendResolvedFilename<false>(process, state, fd, strBuf);
    strBuf << ")";
}

void Close::exit(processState& process, middleend::MiddleEndState& state,
                 long syscallRetval) {
    if (syscallRetval == 0) {
        state.getFilePath<true>(process.pid, fd);
        state.closeFileDescriptor(process.pid, fd);
    }
}

void GetDents::exit(processState& process, middleend::MiddleEndState& state,
                    long syscallRetval) {
    if (syscallRetval == 0) {
        state.listDirectory(process.pid, fd);
    }
}
} // namespace frontend::SyscallHandlers