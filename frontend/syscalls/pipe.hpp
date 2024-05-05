#pragma once
#include "genericSyscallHeader.hpp"
namespace frontend::SyscallHandlers {
	struct Pipe : public simpleSyscallHandler_base {
		long pipesPtr;
		// Inherited via simpleSyscallHandler_base
		void entry(processState& process, const middleend::MiddleEndState& state, long syscallNr) override;
		void exit(processState& process, middleend::MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr) override;
	};

HandlerClassDef(SYS_pipe) : public Pipe{};
HandlerClassDef(SYS_pipe2) : public Pipe{};
}