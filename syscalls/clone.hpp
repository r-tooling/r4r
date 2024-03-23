#pragma once
#include "genericSyscallHeader.hpp"

struct Clone : simpleSyscallHandler_base {
	long flags;
	// Inherited via simpleSyscallHandler_base
	void entry(const processState& process, const MiddleEndState& state, long syscallNr) override;
	void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
	void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
};
/*		else if (syscallNr == SYS_fork || syscallNr == SYS_vfork || syscallNr == SYS_clone || syscallNr == SYS_clone3) {

		}*/
HandlerClassDef(SYS_clone) : public Clone{};