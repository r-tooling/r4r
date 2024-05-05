#include "event.hpp"
namespace frontend::SyscallHandlers {

	void Event::exit(processState& process, middleend::MiddleEndState& state, long syscallRetval)
	{
		if (syscallRetval >= 0) {
			state.registerEventFD(process.pid, syscallRetval);
		}
	}

	void Event::entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr)
	{
		simpleSyscallHandler_base::entryLog(process, state, syscallNr);
	}
}