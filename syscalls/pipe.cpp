#include "pipe.hpp"

void Pipe::entry(const processState& process, const MiddleEndState& state, long syscallNr)
{
	pipesPtr = getSyscallParam<1>(process.pid);
	//todo: do we ever care about O_NONBLOCK and O_CLOEXEC
}

void Pipe::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval == 0) {
		auto pipes = userPtrToOwnPtr<fileDescriptor, 2>(process.pid, pipesPtr);
		state.registerPipe(process.pid, pipes.get());
	}
}

void Pipe::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr);
}
