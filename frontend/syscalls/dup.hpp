#pragma once
#include "genericSyscallHeader.hpp"

namespace frontend::SyscallHandlers {
	struct Dup23 : simpleSyscallHandler_base {
		fileDescriptor oldFd;
		fileDescriptor newFd;
		// Inherited via simpleSyscallHandler_base
		void entry(processState& process, const middleend::MiddleEndState& state, long syscallNr) override;
		void exit(processState& process, middleend::MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr) override;
	};
	struct Dup : simpleSyscallHandler_base {
		fileDescriptor oldFd;
		// Inherited via simpleSyscallHandler_base
		void entry(processState& process, const middleend::MiddleEndState& state, long syscallNr) override;
		void exit(processState& process, middleend::MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr) override;
	};
HandlerClassDef(SYS_dup) : public SyscallHandlers::Dup{};
HandlerClassDef(SYS_dup2) : public SyscallHandlers::Dup23{};
HandlerClassDef(SYS_dup3) : public SyscallHandlers::Dup23{};
}
