#include "execve.hpp"

void Exec::entry(const processState& process, const MiddleEndState& state, long syscallNr)
{
	fileRelPath = std::filesystem::path{ userPtrToString(process.pid, getSyscallParam<1>(process.pid)) };
}

void Exec::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
		std::unique_ptr<char, decltype(std::free)*> resolvedPath{ realpath(fileRelPath.c_str(), nullptr), std::free };//avoid having to re-implement this in the middle end and possibly add more bugs in the implementation. TODD: handle chroot.
		if (!resolvedPath) {
			printf("Unable to resolve path for exec %s\n", fileRelPath.c_str());
			std::string tmp{ fileRelPath };
			state.execFile(process.pid, std::move(tmp), std::move(fileRelPath));
		}
		else {
			state.execFile(process.pid, { resolvedPath.get() }, std::move(fileRelPath));
		}
}

void Exec::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr);
	strBuf << "(" << fileRelPath << ")";
}
