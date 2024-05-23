#include "mkdir.hpp"
namespace frontend::SyscallHandlers {

	void Mkdir::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		at = AT_FDCWD;
		fileRelPath = process.ptrToStr<relFilePath>( process.getSyscallParam<1>());
	}

	void MkdirAt::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		at = process.getSyscallParam<1>();
		fileRelPath = process.ptrToStr<relFilePath>(process.getSyscallParam<2>());
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