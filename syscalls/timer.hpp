#pragma once
#include "./genericSyscallHeader.hpp"

//timerfd_create
namespace SyscallHandlers {

	struct TimerfdCreate : simpleSyscallHandler_base{
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
	};
}

HandlerClassDef(SYS_timerfd_create) : public SyscallHandlers::TimerfdCreate{};
NullOptHandlerClass(SYS_timerfd_settime) //TODO: check integrity