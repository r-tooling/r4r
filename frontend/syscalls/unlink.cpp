#include "unlink.hpp"
namespace frontend::SyscallHandlers {
	void UnlinkBase::exit(processState& process, middleend::MiddleEndState& state, long syscallRetval)
	{
		//fprintf(stderr, "unlink %s %ld\n", fileRelPath.c_str(), syscallRetval);
		if (syscallRetval == 0) {
			state.removeNonDirectory(process.pid, state.resolveToAbsoluteDeleted(process.pid, fileRelPath));
		}
	}

	void RmdirBase::exit(processState& process, middleend::MiddleEndState& state, long syscallRetval)
	{
		//fprintf(stderr, "rmdir %s %ld\n", fileRelPath.c_str(), syscallRetval);
		if (syscallRetval == 0) {
			state.removeDirectory(process.pid, state.resolveToAbsoluteDeleted(process.pid, fileRelPath));
		}
	}

	void Unlink::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		at = AT_FDCWD;
		fileRelPath = userPtrToString(process.pid, getSyscallParam<1>(process.pid));
	}

	void Rmdir::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		at = AT_FDCWD;
		fileRelPath = userPtrToString(process.pid, getSyscallParam<1>(process.pid));
	}

	void UnlinkAt::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		at = getSyscallParam<1>(process.pid);
		fileRelPath = userPtrToString(process.pid, getSyscallParam<2>(process.pid));
		int flags = getSyscallParam<3>(process.pid);
		rmdirType = (flags & AT_REMOVEDIR) != 0;
	}

	void UnlinkAt::exit(processState& process, middleend::MiddleEndState& state, long syscallRetval)
	{
		//fprintf(stderr, "rm %s %d\n", fileRelPath.c_str(), rmdirType ? 1 : 0);
		if (rmdirType)
			RmdirBase::exit(process, state, syscallRetval);
		else
			UnlinkBase::exit(process, state, syscallRetval);
	}

	void UnlinkAt::entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr)
	{
		if (rmdirType)
			RmdirBase::entryLog(process, state, syscallNr);
		else
			UnlinkBase::entryLog(process, state, syscallNr);
		strBuf << "as " << (rmdirType ? "rmdir" : "unlink");
	}

	void PathAtHolder::entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr)
	{
		simpleSyscallHandler_base::entryLog(process, state, syscallNr);
		strBuf << "(";
		appendResolvedFilename(process, state, at, strBuf);
		strBuf << "," << fileRelPath << ")";
	}
}