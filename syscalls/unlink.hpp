#pragma once
#include "genericSyscallHeader.hpp"
#include <filesystem>
namespace SyscallHandlers {
	struct UnlinkBase : virtual simpleSyscallHandler_base, virtual PathAtHolder {
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
	};

	struct RmdirBase : virtual simpleSyscallHandler_base, virtual PathAtHolder {
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
	};

	struct Unlink : UnlinkBase {
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
	};
	struct Rmdir : RmdirBase {
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
	};
	struct UnlinkAt : UnlinkBase, RmdirBase {
		bool rmdirType;

		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
	};
}

HandlerClassDef(SYS_rmdir) : public SyscallHandlers::Rmdir{};
HandlerClassDef(SYS_unlink) : public SyscallHandlers::Unlink{};
HandlerClassDef(SYS_unlinkat) : public SyscallHandlers::UnlinkAt{};