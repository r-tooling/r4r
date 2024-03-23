#pragma once
#include "genericSyscallHeader.hpp"

struct Pipe : simpleSyscallHandler_base{
	long pipesPtr;
	// Inherited via simpleSyscallHandler_base
	void entry(const processState& process, const MiddleEndState& state, long syscallNr) override;
	void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
	void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
};

HandlerClassDef(SYS_pipe) : public Pipe{};
HandlerClassDef(SYS_pipe2) : public Pipe{};