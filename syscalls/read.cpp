#include "read.hpp"

void FileOperationLogger::entry(const processState& process, const MiddleEndState& state, long syscallNr)
{
	fd = getSyscallParam<1>(process.pid);
}

void FileOperationLogger::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	//we just log this. no real operations happening
}

void FileOperationLogger::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process,state, syscallNr);
	strBuf << "(";
	appendResolvedFilename(process, state, fd, strBuf);
	strBuf << ")";
}

void Close::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval == 0) {
		state.closeFile(process.pid, fd);
	}
}
