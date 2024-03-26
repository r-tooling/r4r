#pragma once
#include "./genericSyscallHeader.hpp"
#include <fcntl.h>
namespace SyscallHandlers {
	struct Fcntl : public simpleSyscallHandler_base {
		fileDescriptor oldFd;
		enum {
			dup,
			nullopt
		} syscallHandling;
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;

		void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
	};
}

HandlerClassDef(SYS_fcntl) : public SyscallHandlers::Fcntl{};