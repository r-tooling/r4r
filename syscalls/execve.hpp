#pragma once
#include "genericSyscallHeader.hpp"

struct Exec : simpleSyscallHandler_base {
	std::filesystem::path fileRelPath;

	// Inherited via simpleSyscallHandler_base
	void entry(const processState& process, const MiddleEndState& state, long syscallNr) override;
	void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
	void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;
	// Inherited via simpleSyscallHandler_base
};

HandlerClassDef(SYS_execve) : public Exec{};