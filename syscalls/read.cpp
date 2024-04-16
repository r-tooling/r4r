#include "read.hpp"

void SyscallHandlers::FileOperationLogger::entry(processState & process, const MiddleEndState& , long )
{
	fd = getSyscallParam<1>(process.pid);
}

void SyscallHandlers::FileOperationLogger::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	//we just log this. no "real operations happening
	if (syscallRetval >= 0) {
		state.getFilePath<true>(process.pid, fd);
	}
}

void SyscallHandlers::FileOperationLogger::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process,state, syscallNr);
	strBuf << "(";
	appendResolvedFilename<false>(process, state, fd, strBuf);
	strBuf << ")";
}

void SyscallHandlers::Close::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval == 0) {
		state.getFilePath<true>(process.pid, fd);
		state.closeFileDescriptor(process.pid, fd);
	}
}

void SyscallHandlers::GetDents::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval == 0) {
		state.listDirectory(process.pid, fd);
	}
}
