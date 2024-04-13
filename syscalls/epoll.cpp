#include "epoll.hpp"

void SyscallHandlers::EpollCreate::entry(processState& process, const MiddleEndState& state, long syscallNr)
{
	//todo: in the case of epoll_create1 CLOSE ON EXEC
}

void SyscallHandlers::EpollCreate::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval >= 0) {
		state.registerEpoll(process.pid, syscallRetval);
	}
}

void SyscallHandlers::EpollCreate::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr);
}