#include "socket.hpp"

void SyscallHandlers::Socket::entry(processState& process, const MiddleEndState& state, long syscallNr)
{
}

void SyscallHandlers::Socket::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval >= 0) {
		state.registerSocket(process.pid, syscallRetval);
	}
}

void SyscallHandlers::Socket::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr); //nothing for now, will add flags
}

void SyscallHandlers::SocketPair::entry(processState& process, const MiddleEndState& state, long syscallNr)
{
	ptr = getSyscallParam<4>(process.pid);
}

void SyscallHandlers::SocketPair::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval == 0) {
		auto pipes = userPtrToOwnPtr<fileDescriptor, 2>(process.pid, ptr);
		state.registerSocket(process.pid, pipes.get());
	}
}

void SyscallHandlers::SocketPair::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr);
}
