#pragma once
#include "genericSyscallHeader.hpp"
#include <filesystem>
namespace SyscallHandlers {
	struct OpenBase : simpleSyscallHandler_base {
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;

		void checkIfExists(const processState& process);

		std::filesystem::path fileRelPath;
		long flags;
		fileDescriptor at;
		bool existed;
	};

	struct Open : OpenBase {
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
	};
	struct Creat : OpenBase {
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
	};

	struct OpenAT : OpenBase {
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
	};
	struct OpenAT2 : OpenBase {
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
	};

}

HandlerClassDef(SYS_open) : public SyscallHandlers::Open{};
HandlerClassDef(SYS_openat) : public SyscallHandlers::OpenAT{};
HandlerClassDef(SYS_openat2) : public SyscallHandlers::OpenAT2{};

HandlerClassDef(SYS_creat) : public SyscallHandlers::Creat{};
