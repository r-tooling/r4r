#pragma once
#include "genericSyscallHeader.hpp"
#include <variant>
namespace SyscallHandlers {
	struct StatHandler : public simpleSyscallHandler_base {

		fileDescriptor at;
		std::filesystem::path path;
		int flags;
		//todo: do I ever care for the result?
		// Inherited via simpleSyscallHandler_base
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
	};
	struct Stat : StatHandler {
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
	};
	struct FStat : StatHandler {
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
	};
	struct LStat : StatHandler {
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
	};
	struct NewFStatAt : StatHandler {
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
	};
	struct StatX : StatHandler {
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
	};
}

HandlerClassDef(SYS_stat) : public SyscallHandlers::Stat{};
HandlerClassDef(SYS_fstat) : public SyscallHandlers::FStat{};
HandlerClassDef(SYS_lstat) : public SyscallHandlers::LStat{};
HandlerClassDef(SYS_newfstatat) : public SyscallHandlers::NewFStatAt{};
HandlerClassDef(SYS_statx) : public SyscallHandlers::StatX{};

namespace SyscallHandlers {
	struct AccessHandler : public simpleSyscallHandler_base {

		fileDescriptor at;
		std::filesystem::path path;
		long flags;
		//todo: do I ever care for the result?
		// Inherited via simpleSyscallHandler_base
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
	};
	struct Access : StatHandler {
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
	};
	struct FAccessAt : StatHandler {
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
	};
	struct FAccessAt2 : StatHandler {
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
	};
}
HandlerClassDef(SYS_access) : public SyscallHandlers::Access{};
HandlerClassDef(SYS_faccessat) : public SyscallHandlers::FAccessAt{};
HandlerClassDef(SYS_faccessat2) : public SyscallHandlers::FAccessAt2{};

namespace SyscallHandlers {
	struct ReadLinkHandler : public simpleSyscallHandler_base {

		fileDescriptor at;
		std::filesystem::path path;
		long userPtr;
		size_t maxBufferSize;
		std::unique_ptr<unsigned char[]> returnedData;
		//todo: do I ever care for the result?
		// Inherited via simpleSyscallHandler_base
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
		void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
		void exitLog(const processState& process, const MiddleEndState& state, long syscallRetval) override;

	};
	struct ReadLink : ReadLinkHandler {
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
	};
	struct ReadLinkAt : ReadLinkHandler {
		void entry(processState& process, const MiddleEndState& state, long syscallNr) override;
	};
}
HandlerClassDef(SYS_readlink) : public SyscallHandlers::ReadLink{};
HandlerClassDef(SYS_readlinkat) : public SyscallHandlers::ReadLinkAt{};


//TODO: ustat statfs fstatfs should probably go elsewhere? Stating the filesystem, not files
NullOptHandlerClass(SYS_statfs);
NullOptHandlerClass(SYS_fstatfs);
NullOptHandlerClass(SYS_ustat);