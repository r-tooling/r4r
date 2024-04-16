#include "timer.hpp"

void SyscallHandlers::TimerfdCreate::entry(processState& , const MiddleEndState& , long )
{//TODO: close on exec
}

void SyscallHandlers::TimerfdCreate::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval >= 0) {
		state.registerTimer(process.pid, syscallRetval);
	}
}

void SyscallHandlers::TimerfdCreate::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr);
}
