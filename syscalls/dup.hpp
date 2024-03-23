#pragma once
#include "genericSyscallHeader.hpp"

struct Dup23 : simpleSyscallHandler_base {
	fileDescriptor oldFd;
	fileDescriptor newFd;
	// Inherited via simpleSyscallHandler_base
	void entry(const processState& process, const MiddleEndState& state, long syscallNr) override;
	void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
	void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
};
struct Dup : simpleSyscallHandler_base {
	fileDescriptor oldFd;
	// Inherited via simpleSyscallHandler_base
	void entry(const processState& process, const MiddleEndState& state, long syscallNr) override;
	void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
	void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
};

HandlerClassDef(SYS_dup) : public Dup{};
HandlerClassDef(SYS_dup2) : public Dup23{};
HandlerClassDef(SYS_dup3) : public Dup23{};