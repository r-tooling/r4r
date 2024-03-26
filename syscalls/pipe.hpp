#pragma once
#include "genericSyscallHeader.hpp"
namespace SyscallHandlers {
	struct Pipe : public simpleSyscallHandler_base {
		long pipesPtr;
		// Inherited via simpleSyscallHandler_base
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
	};
}
HandlerClassDef(SYS_pipe) : public SyscallHandlers::Pipe{};
HandlerClassDef(SYS_pipe2) : public SyscallHandlers::Pipe{};