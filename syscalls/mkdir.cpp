#include "mkdir.hpp"

void SyscallHandlers::Mkdir::entry(processState& process, const MiddleEndState& state, long syscallNr)
{
	at = AT_FDCWD;
	fileRelPath = userPtrToString(process.pid, getSyscallParam<1>(process.pid));
}

void SyscallHandlers::MkdirAt::entry(processState& process, const MiddleEndState& state, long syscallNr)
{
	at = getSyscallParam<1>(process.pid);
	fileRelPath = userPtrToString(process.pid, getSyscallParam<2>(process.pid));
}

void SyscallHandlers::MkdirBase::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	//TODO: search perms in path.
	if (syscallRetval == 0) {
		auto resolvedPath = state.resolveToAbsoltute(process.pid,fileRelPath,at);
		state.createDirectory(process.pid, resolvedPath, std::move(fileRelPath));//the order is undefined otherwise
	}
}

void SyscallHandlers::MkdirBase::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process,state,syscallNr);
	strBuf << "(";
	appendResolvedFilename(process, state, at, strBuf);
	strBuf << "," << fileRelPath << ")";
}
