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
		assert(at == AT_FDCWD || fileRelPath.is_absolute());
		std::unique_ptr<char, decltype(std::free)*> resolvedPath{ realpath(fileRelPath.c_str(), nullptr), std::free };//avoid having to re-implement this in the middle end and possibly add more bugs in the implementation. TODD: handle chroot.

		if (!resolvedPath) {
			printf("Unable to resolve path for file %s\n", fileRelPath.c_str());
			std::string tmp{ fileRelPath };
			state.createDirectory(process.pid, std::move(tmp), std::move(fileRelPath));//the order is undefined otherwise
		}
		else {
			state.createDirectory(process.pid, { resolvedPath.get() }, std::move(fileRelPath));//the order is undefined otherwise
		}
	}
}

void SyscallHandlers::MkdirBase::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process,state,syscallNr);
	strBuf << "(";
	appendResolvedFilename(process, state, at, strBuf);
	strBuf << "," << fileRelPath << ")";
}
