#include "timer.hpp"
namespace frontend::SyscallHandlers {
void TimerfdCreate::entry(processState&, const middleend::MiddleEndState&,
                          long) { // TODO: close on exec
}

void TimerfdCreate::exit(processState& process,
                         middleend::MiddleEndState& state, long syscallRetval) {
    if (syscallRetval >= 0) {
        state.registerTimer(process.pid, syscallRetval);
    }
}

void TimerfdCreate::entryLog(const processState& process,
                             const middleend::MiddleEndState& state,
                             long syscallNr) {
    simpleSyscallHandler_base::entryLog(process, state, syscallNr);
}
} // namespace frontend::SyscallHandlers