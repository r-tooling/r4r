#pragma once
#include "genericSyscallHeader.hpp"
namespace SyscallHandlers {
	struct Socket : simpleSyscallHandler_base {
		// Inherited via simpleSyscallHandler_base
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
	};
}

HandlerClassDef(SYS_socket) : public SyscallHandlers::Socket{};
namespace SyscallHandlers {
	struct SocketPair : simpleSyscallHandler_base {
		long ptr;
		// Inherited via simpleSyscallHandler_base
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
	};
}
HandlerClassDef(SYS_socketpair) : public SyscallHandlers::SocketPair{};
