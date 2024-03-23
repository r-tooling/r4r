#pragma once
#include "genericSyscallHeader.hpp"

struct FileOperationLogger : simpleSyscallHandler_base {
	fileDescriptor fd;
	// Inherited via simpleSyscallHandler_base
	void entry(const processState& process, const MiddleEndState& state, long syscallNr) override;
	void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
	void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
};

struct Close : FileOperationLogger {
	void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
};

HandlerClassDef(SYS_read) : public FileOperationLogger{};
HandlerClassDef(SYS_pread64) : public FileOperationLogger{};
HandlerClassDef(SYS_write) : public FileOperationLogger{};
HandlerClassDef(SYS_pwrite64) : public FileOperationLogger{};

HandlerClassDef(SYS_close) : public Close{};

HandlerClassDef(SYS_lseek) : public FileOperationLogger{};

//TODO: mark the given directores as needing access to this!
HandlerClassDef(SYS_getdents) : public FileOperationLogger{};
HandlerClassDef(SYS_getdents64) : public FileOperationLogger{};