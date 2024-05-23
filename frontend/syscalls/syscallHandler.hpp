#pragma once

namespace frontend {
	struct processState;
}
namespace frontend::SyscallHandlers {
	struct SyscallHandler;
};

#include "../ptraceMainLoop.hpp"

//circular dependency hell resolution

//this is just a wrapper to ensure the API is set.
namespace frontend::SyscallHandlers {
	struct SyscallHandler {
		/*
			A handler for whatever needs to be done with the arguments at syscall entry.
			The middle-end should be unaffected as the syscall may not execute correctly.
			If anything, the exit handler should then be called on for example termination of the program.
		*/
		virtual void entry(processState& process, const middleend::MiddleEndState& state, long syscallNr) = 0;
		/*
			The exit handler is used to persist the syscall information into the middle end.
		*/
		virtual void exit(processState& process, middleend::MiddleEndState& state, long syscallRetval) = 0;
		/*
			Called right after entry() used only for logging, may be disabled.
		*/
		virtual void entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr) = 0;
		/*
			Called right before exit() used only for logging, may be disabled.
		*/
		virtual void exitLog(const processState& process, const middleend::MiddleEndState& state, long syscallRetval) = 0;

		virtual ~SyscallHandler() noexcept = default;
	};
};