#pragma once
#include "genericSyscallHeader.hpp"

struct onlyEntryLog : public simpleSyscallHandler_base {
	void entry(const processState& process, const MiddleEndState& state, long syscallNr) override {};
	void exit(processState& process, MiddleEndState& state, long syscallRetval) override {};

	void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override {
		simpleSyscallHandler_base::entryLog(process,state,syscallNr);
		strBuf << "() = exiting\n";
		printf("%d : %s",process.pid, strBuf.str().c_str());
		strBuf.clear();
	};
	void exitLog(const processState& process, const MiddleEndState& state, long syscallRetval) override {};
};

HandlerClassDef(SYS_exit_group) : public onlyEntryLog{};
HandlerClassDef(SYS_exit) : public onlyEntryLog{};

