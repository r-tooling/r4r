#include "chroot.hpp"
namespace frontend::SyscallHandlers {
void Chdir::entry(processState& process, const middleend::MiddleEndState&,
                  long) {
    newPath = process.ptrToStr(process.getSyscallParam<1>());
}

void Chdir::exit(processState& process, middleend::MiddleEndState& state,
                 long syscallRetval) {
    if (syscallRetval == 0) {
        state.changeDirectory(process.pid, newPath);
    }
}

void Chdir::entryLog(const processState& process,
                     const middleend::MiddleEndState& state, long syscallNr) {
    simpleSyscallHandler_base::entryLog(process, state, syscallNr);
    strBuf << "(" << newPath << ")";
}

void Chdir::exitLog(const processState& process,
                    const middleend::MiddleEndState& state,
                    long syscallRetval) {
    if (syscallRetval == 0) {
        auto str = strBuf.str();
        decltype(auto) path = state.getCWD(process.pid).native();
        printf("%d: %s = %s\n", process.pid,
               str.empty() ? "Unknown syscall" : str.c_str(), path.c_str());
        strBuf.clear();
    } else {
        simpleSyscallHandler_base::exitLog(process, state, syscallRetval);
    }
}

void GetCWD::entry(processState& process, const middleend::MiddleEndState&,
                   long) {
    ptr = process.getSyscallParam<1>();
}

void GetCWD::exit(processState& process, middleend::MiddleEndState& state,
                  long syscallRetval) {
    if (syscallRetval > 0) {
        auto str = std::filesystem::path(process.ptrToStr(ptr));
        (void)state;
        assert(str == state.getCWD(process.pid)); // TODO: remove me for
                                                  // release.
    }
}
} // namespace frontend::SyscallHandlers