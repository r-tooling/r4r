#pragma once
#include "./genericSyscallHeader.hpp"
#include <fcntl.h>

struct fnctlHandler : public simpleSyscallHandler_base {
	fileDescriptor oldFd;
	enum{
		dup,
		nullopt
	} syscallHandling;
	void entry(const processState& process, const MiddleEndState& state, long syscallNr) override;
	void exit(processState& process, MiddleEndState& state, long syscallRetval) override;

	void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
};

HandlerClassDef(SYS_fcntl) : public fnctlHandler{};