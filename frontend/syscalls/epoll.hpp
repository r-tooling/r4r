#pragma once
#include "./genericSyscallHeader.hpp"

namespace frontend::SyscallHandlers {

	struct EpollCreate : simpleSyscallHandler_base {
		void entry(processState& process, const middleend::MiddleEndState& state, long syscallNr) override;
		void exit(processState& process, middleend::MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr) override;
	};

HandlerClassDef(SYS_epoll_create) : public SyscallHandlers::EpollCreate{};
HandlerClassDef(SYS_epoll_create1) : public SyscallHandlers::EpollCreate{};
NullOptHandlerClass(SYS_epoll_ctl); //TODO: add fd type validity checks.
NullOptHandlerClass(SYS_epoll_wait); //TODO: add fd type validity checks.

}
