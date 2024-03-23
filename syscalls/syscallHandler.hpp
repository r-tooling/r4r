#pragma once

#include "../ptraceMainLoop.hpp"

//this is just a wrapper to ensure the API is set.
struct syscallHandler {

	virtual void entry(const processState& process, const MiddleEndState& state, long syscallNr) = 0;
	virtual void exit(processState& process, MiddleEndState& state, long syscallRetval) = 0;

	virtual void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) = 0;
	virtual void exitLog(const processState& process, const MiddleEndState& state, long syscallRetval) = 0;

	virtual ~syscallHandler() = default;
};