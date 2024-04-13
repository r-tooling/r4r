#pragma once
#include "genericSyscallHeader.hpp"
namespace SyscallHandlers{
	struct Close : FileOperationLogger {
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
	};
	struct GetDents : FileOperationLogger {
		void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
	};
}

HandlerClassDef(SYS_ftruncate) : public SyscallHandlers::FileOperationLogger{};

HandlerClassDef(SYS_read) : public SyscallHandlers::FileOperationLogger{};
HandlerClassDef(SYS_pread64) : public SyscallHandlers::FileOperationLogger{};
HandlerClassDef(SYS_write) : public SyscallHandlers::FileOperationLogger{};
HandlerClassDef(SYS_pwrite64) : public SyscallHandlers::FileOperationLogger{};

HandlerClassDef(SYS_close) : public SyscallHandlers::Close{};

HandlerClassDef(SYS_lseek) : public SyscallHandlers::FileOperationLogger{};

//TODO: maybe handle the known cases here?
HandlerClassDef(SYS_ioctl) : public SyscallHandlers::FileOperationLogger{};//TODO: only log unable to resolve on non-EBADF  

//TODO: mark the given directores as needing access to this!
HandlerClassDef(SYS_getdents) : public SyscallHandlers::FileOperationLogger{};
HandlerClassDef(SYS_getdents64) : public SyscallHandlers::FileOperationLogger{};

