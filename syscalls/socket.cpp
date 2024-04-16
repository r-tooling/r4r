#include "socket.hpp"

void SyscallHandlers::Socket::entry(processState& process, const MiddleEndState& , long )
{
	parseDomain(process.getSyscallParam<1>());
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
	printDomain(strBuf);
}

void SyscallHandlers::SocketPair::entry(processState& process, const MiddleEndState&, long)
{
	ptr = process.getSyscallParam<4>();
	parseDomain(process.getSyscallParam<1>());
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
	printDomain(strBuf);
}

void SyscallHandlers::Connect::entry(processState& process, const MiddleEndState&, long)
{
	fd = process.getSyscallParam<1>();
	ptr = process.getSyscallParam<2>();
	structSize = process.getSyscallParam<3>();
}

void SyscallHandlers::Connect::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	(void)process; (void)state; (void)syscallRetval;
	if (syscallRetval == 0) {
		//auto addr = userPtrToOwnPtr<struct sockaddr>(process.pid, ptr);
		//socketFileInfo info = state.resolveSocketData(process.pid, fd);
		//TODO: add logging of the param values based on the family..
		//we know the relevant length, no need to do the runabout with using sockaddr_storage 

	}//TODO: otherwise it was invalid anyhow, though here it may be relevant to see where a connection was attempted to
	
}

void SyscallHandlers::Connect::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr);
}
