#include "epoll.hpp"
namespace frontend::SyscallHandlers {

	void EpollCreate::entry(processState&, const middleend::MiddleEndState&, long)
	{
		//todo: in the case of epoll_create1 CLOSE ON EXEC
	}

	void EpollCreate::exit(processState& process, middleend::MiddleEndState& state, long syscallRetval)
	{
		if (syscallRetval >= 0) {
			state.registerEpoll(process.pid, syscallRetval);
		}
	}

	void EpollCreate::entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr)
	{
		simpleSyscallHandler_base::entryLog(process, state, syscallNr);
	}
}