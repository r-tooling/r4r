#pragma once
#include "genericSyscallHeader.hpp"

namespace SyscallHandlers {
	struct Event : simpleSyscallHandler_base {
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override
		{//nullopt
		};
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
	};
}
HandlerClassDef(SYS_eventfd) : public SyscallHandlers::Event{};//TODO: close on exec
HandlerClassDef(SYS_eventfd2) : public SyscallHandlers::Event{};
