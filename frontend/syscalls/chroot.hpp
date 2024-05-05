#pragma once
#include "genericSyscallHeader.hpp"

namespace frontend::SyscallHandlers {
	struct Chdir : simpleSyscallHandler_base {

		std::filesystem::path newPath;

		void entry(processState& process, const middleend::MiddleEndState& state, long syscallNr) override;
		void exit(processState& process, middleend::MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr) override;
		void exitLog(const processState& process, const middleend::MiddleEndState& state, long syscallRetval) override;
	};

	struct GetCWD : NullOptHandler { //don't even bother logging this
		long ptr;
		void entry(processState& process, const middleend::MiddleEndState& state, long syscallNr) override;
		void exit(processState& process, middleend::MiddleEndState& state, long syscallRetval) override;
	};


HandlerClassDef(SYS_chroot) : public ErrorHandler{};
HandlerClassDef(SYS_chdir) : public Chdir{};
HandlerClassDef(SYS_getcwd) : public GetCWD{};
}