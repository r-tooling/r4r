#pragma once
#include "genericSyscallHeader.hpp"
namespace frontend::SyscallHandlers{
	struct Close : FileOperationLogger {
		void exit(processState& process, middleend::MiddleEndState& state, long syscallRetval) override;
	};
	struct GetDents : FileOperationLogger {
		void exit(processState& process, middleend::MiddleEndState& state, long syscallRetval) override;
	};

HandlerClassDef(SYS_ftruncate) : public FileOperationLogger{};

HandlerClassDef(SYS_read) : public FileOperationLogger{};
HandlerClassDef(SYS_pread64) : public FileOperationLogger{};
HandlerClassDef(SYS_write) : public FileOperationLogger{};
HandlerClassDef(SYS_pwrite64) : public FileOperationLogger{};

HandlerClassDef(SYS_close) : public Close{};

HandlerClassDef(SYS_lseek) : public FileOperationLogger{};

//TODO: maybe handle the known cases here?
HandlerClassDef(SYS_ioctl) : public FileOperationLogger{};//TODO: only log unable to resolve on non-EBADF  

HandlerClassDef(SYS_getdents) : public GetDents{};
HandlerClassDef(SYS_getdents64) : public GetDents{};

}
