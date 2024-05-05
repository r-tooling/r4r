#include "execve.hpp"
namespace frontend::SyscallHandlers {

	void Exec::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		fileRelPath = std::filesystem::path{ userPtrToString(process.pid, getSyscallParam<1>(process.pid)) };
	}

	void Exec::exit(processState& process, middleend::MiddleEndState& state, long syscallRetval)
	{
		auto resolvedPath = state.resolveToAbsoltute(process.pid, fileRelPath);//avoid having to re-implement this in the middle end and possibly add more bugs in the implementation. TODD: handle chroot.
		auto failed = state.execFile(process.pid, resolvedPath, fileRelPath, 0, false);
		if (failed && syscallRetval == 0) {
			state.execFile(process.pid, resolvedPath, std::move(fileRelPath), 0, true); // Middle end decided the exec should fail but it does not. Force it to keep a hold of this reality.
		}

	}

	void Exec::entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr)
	{
		simpleSyscallHandler_base::entryLog(process, state, syscallNr);
		strBuf << "(" << fileRelPath << ")";
	}
}