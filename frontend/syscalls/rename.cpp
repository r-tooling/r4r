#include "rename.hpp"

void frontend::SyscallHandlers::Rename::entry(processState& process, const middleend::MiddleEndState&, long)
{
	at_src = at_dst = AT_FDCWD;
	src = process.ptrToStr<std::filesystem::path>(process.getSyscallParam<1>());
	dst = process.ptrToStr<std::filesystem::path>(process.getSyscallParam<2>());
}

void frontend::SyscallHandlers::RenameAT::entry(processState& process, const middleend::MiddleEndState&, long )
{
	at_src = process.getSyscallParam<1>();
	src = process.ptrToStr<std::filesystem::path>(process.getSyscallParam<2>());
	at_dst = process.getSyscallParam<3>();
	dst = process.ptrToStr<std::filesystem::path>(process.getSyscallParam<4>());
}

void frontend::SyscallHandlers::RenameAT2::entry(processState& process, const middleend::MiddleEndState&, long )
{
	at_src = process.getSyscallParam<1>();
	src = process.ptrToStr<std::filesystem::path>(process.getSyscallParam<2>());
	at_dst = process.getSyscallParam<3>();
	dst = process.ptrToStr<std::filesystem::path>(process.getSyscallParam<4>());
	flags = process.getSyscallParam<5>();
}

void frontend::SyscallHandlers::RenameBase::exit(processState&, middleend::MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval > 0) {
		//TODO: mark the files as moved around.
		state.syscallWarn(SYS_rename, "The rename syscall semantics are not currently handled");
	}
}

void frontend::SyscallHandlers::RenameBase::entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr);
	strBuf << state.getFilePath<false>(process.pid, at_src).value_or("unknown") << " - " << src << state.getFilePath<false>(process.pid, at_dst).value_or("unknown") << " - " << dst;
}
