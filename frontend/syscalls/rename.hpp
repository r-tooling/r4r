#pragma once
#include "genericSyscallHeader.hpp"
#include <filesystem>
namespace frontend::SyscallHandlers {
	struct RenameBase : simpleSyscallHandler_base {
		void exit(processState& process, middleend::MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr) override;

		fileDescriptor at_src;
		relFilePath src;
		fileDescriptor at_dst;
		relFilePath dst;
		int flags;
	};

	struct Rename : RenameBase {
		void entry(processState& process, const middleend::MiddleEndState& state, long syscallNr) override;
	};
	struct RenameAT : RenameBase {
		void entry(processState& process, const middleend::MiddleEndState& state, long syscallNr) override;
	};
	struct RenameAT2 : RenameBase {
		void entry(processState& process, const middleend::MiddleEndState& state, long syscallNr) override;
	};


	HandlerClassDef(SYS_rename) : public Rename{};
	HandlerClassDef(SYS_renameat) : public RenameAT{};
	HandlerClassDef(SYS_renameat2) : public RenameAT2{};

}