#include "execve.hpp"

void SyscallHandlers::Exec::entry(processState & process, const MiddleEndState& state, long syscallNr)
{
	fileRelPath = std::filesystem::path{ userPtrToString(process.pid, getSyscallParam<1>(process.pid)) };
}

void SyscallHandlers::Exec::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
		auto resolvedPath = state.resolveToAbsoltute(process.pid, fileRelPath);//avoid having to re-implement this in the middle end and possibly add more bugs in the implementation. TODD: handle chroot.
		auto failed = state.execFile(process.pid, resolvedPath, std::move(fileRelPath),0,false);
		if (failed && syscallRetval == 0) {
			state.execFile(process.pid, resolvedPath, std::move(fileRelPath), 0, false); // Middle end decided the exec should fail but it does not. Force it to keep a hold of this reality.
		}

}

void SyscallHandlers::Exec::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr);
	strBuf << "(" << fileRelPath << ")";
}
