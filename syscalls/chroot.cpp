#include "chroot.hpp"

void SyscallHandlers::Chdir::entry(processState& process, const MiddleEndState& , long )
{
	newPath = process.ptrToStr(process.getSyscallParam<1>());
}

void SyscallHandlers::Chdir::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval == 0) {
		state.changeDirectory(process.pid, newPath);
	}

}

void SyscallHandlers::Chdir::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr);
	strBuf << "(" << newPath << ")";
}

void SyscallHandlers::Chdir::exitLog(const processState& process, const MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval == 0) {
		auto str = strBuf.str();
		decltype(auto) path = state.getCWD(process.pid).native();
		printf("%d: %s = %s\n", process.pid, str.empty() ? "Unknown syscall" : str.c_str(), path.c_str());
		strBuf.clear();
	}
	else {
		simpleSyscallHandler_base::exitLog(process, state, syscallRetval);
	}
}

void SyscallHandlers::GetCWD::entry(processState& process, const MiddleEndState& , long )
{
	ptr = process.getSyscallParam<1>();
}

void SyscallHandlers::GetCWD::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval > 0) {
		auto str = std::filesystem::path(process.ptrToStr(ptr));
		assert(str == state.getCWD(process.pid)); //TODO: remove me for release.
	}
}
