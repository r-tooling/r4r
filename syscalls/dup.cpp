#include "dup.hpp"

void Dup23::entry(const processState& process, const MiddleEndState& state, long syscallNr)
{
	oldFd = getSyscallParam<1>(process.pid);
	newFd = getSyscallParam<2>(process.pid);
	//todo: do we care about O_CLOEXEC?
}

void Dup23::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval >= 0) {
		state.registerFdAlias(process.pid, newFd, oldFd);
	}
}

void Dup23::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process,state, syscallNr);
	strBuf << "("<< newFd << " = ";
	appendResolvedFilename(process, state, oldFd, strBuf);
	strBuf << ")";
}

void Dup::entry(const processState& process, const MiddleEndState& state, long syscallNr)
{
	oldFd = getSyscallParam<1>(process.pid);
}

void Dup::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval >= 0) {
		state.registerFdAlias(process.pid, syscallRetval, oldFd);
	}
}

void Dup::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr);
	strBuf << "(retval = ";
	appendResolvedFilename(process, state, oldFd, strBuf);
	strBuf << ")";
}
