#include "event.hpp"



void SyscallHandlers::Event::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval >= 0) {
		state.registerEventFD(process.pid, syscallRetval);
	}
}

void SyscallHandlers::Event::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr);
}
