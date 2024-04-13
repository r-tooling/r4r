#pragma once
#include "genericSyscallHeader.hpp"

namespace SyscallHandlers {
	struct Chdir : simpleSyscallHandler_base {

		std::filesystem::path newPath;

		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
		void exitLog(const processState& process, const MiddleEndState& state, long syscallRetval) override;
	};

	struct GetCWD : nullOptHandler { //don't even bother logging this
		long ptr;
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
	};
}

HandlerClassDef(SYS_chroot) : public errorHandler{};
HandlerClassDef(SYS_chdir) : public SyscallHandlers::Chdir{};
HandlerClassDef(SYS_getcwd) : public SyscallHandlers::GetCWD{};