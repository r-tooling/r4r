#include "dup.hpp"

void SyscallHandlers::Dup23::entry(processState & process, const MiddleEndState& state, long syscallNr)
{
	oldFd = getSyscallParam<1>(process.pid);
	newFd = getSyscallParam<2>(process.pid);
	//todo: do we care about O_CLOEXEC?
}

void SyscallHandlers::Dup23::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval >= 0) {
		state.registerFdAlias(process.pid, newFd, oldFd);
	}
}

void SyscallHandlers::Dup23::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process,state, syscallNr);
	strBuf << "("<< newFd << " = ";
	appendResolvedFilename<false>(process, state, oldFd, strBuf);
	strBuf << ")";
}

void SyscallHandlers::Dup::entry(processState & process, const MiddleEndState& state, long syscallNr)
{
	oldFd = getSyscallParam<1>(process.pid);
}

void SyscallHandlers::Dup::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval >= 0) {
		state.registerFdAlias(process.pid, syscallRetval, oldFd);
	}
}

void SyscallHandlers::Dup::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr);
	strBuf << "(retval = ";
	appendResolvedFilename(process, state, oldFd, strBuf);
	strBuf << ")";
}
