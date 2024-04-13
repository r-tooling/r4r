#include "unlink.hpp"

void SyscallHandlers::UnlinkBase::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	//fprintf(stderr, "unlink %s %ld\n", fileRelPath.c_str(), syscallRetval);
	if (syscallRetval == 0) {
		state.removeNonDirectory(process.pid,state.resolveToAbsoltuteDeleted(process.pid, fileRelPath));
	}
}

void SyscallHandlers::RmdirBase::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	//fprintf(stderr, "rmdir %s %ld\n", fileRelPath.c_str(), syscallRetval);
	if (syscallRetval == 0) {
		state.removeDirectory(process.pid, state.resolveToAbsoltuteDeleted(process.pid, fileRelPath));
	}
}

void SyscallHandlers::Unlink::entry(processState& process, const MiddleEndState& state, long syscallNr)
{
	at = AT_FDCWD;
	fileRelPath = userPtrToString(process.pid,getSyscallParam<1>(process.pid));
}

void SyscallHandlers::Rmdir::entry(processState& process, const MiddleEndState& state, long syscallNr)
{
	at = AT_FDCWD;
	fileRelPath = userPtrToString(process.pid, getSyscallParam<1>(process.pid));
}

void SyscallHandlers::UnlinkAt::entry(processState& process, const MiddleEndState& state, long syscallNr)
{
	at = getSyscallParam<1>(process.pid);
	fileRelPath = userPtrToString(process.pid, getSyscallParam<2>(process.pid));
	int flags = getSyscallParam<3>(process.pid);
	rmdirType = (flags & AT_REMOVEDIR) != 0;
}

void SyscallHandlers::UnlinkAt::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	//fprintf(stderr, "rm %s %d\n", fileRelPath.c_str(), rmdirType ? 1 : 0);
	if (rmdirType)
		RmdirBase::exit(process,state,syscallRetval);
	else
		UnlinkBase::exit(process, state, syscallRetval);
}

void SyscallHandlers::UnlinkAt::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	if (rmdirType)
		RmdirBase::entryLog(process, state, syscallNr);
	else
		UnlinkBase::entryLog(process, state, syscallNr);
	strBuf << "as " << rmdirType ? "rmdir" : "unlink";
}

void SyscallHandlers::PathAtHolder::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr);
	strBuf << "(";
	appendResolvedFilename(process, state, at, strBuf);
	strBuf << "," << fileRelPath << ")";
}
