#include "mkdir.hpp"
namespace frontend::SyscallHandlers {

	void Mkdir::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		at = AT_FDCWD;
		fileRelPath = userPtrToString(process.pid, getSyscallParam<1>(process.pid));
	}

	void MkdirAt::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		at = getSyscallParam<1>(process.pid);
		fileRelPath = userPtrToString(process.pid, getSyscallParam<2>(process.pid));
	}

	void MkdirBase::exit(processState& process, middleend::MiddleEndState& state, long syscallRetval)
	{
		//TODO: search perms in path.
		if (syscallRetval == 0) {
			auto resolvedPath = state.resolveToAbsolute(process.pid, fileRelPath, at);
			state.createDirectory(process.pid, resolvedPath, std::move(fileRelPath));//the order is undefined otherwise
		}
	}

	void MkdirBase::entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr)
	{
		simpleSyscallHandler_base::entryLog(process, state, syscallNr);
		strBuf << "(";
		appendResolvedFilename(process, state, at, strBuf);
		strBuf << "," << fileRelPath << ")";
	}
}