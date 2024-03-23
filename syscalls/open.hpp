#pragma once
#include "genericSyscallHeader.hpp"
#include <filesystem>

struct OpenBase : simpleSyscallHandler_base {
	void exit(processState& process, MiddleEndState& state, long syscallRetval) override;
	void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override;

	void checkIfExists(const processState& process);

	std::filesystem::path fileRelPath;
	long flags;
	fileDescriptor at;
	bool existed;
};

struct Open : OpenBase {
	void entry(const processState& process, const MiddleEndState& state, long syscallNr) override;
};
struct Creat : OpenBase {
	void entry(const processState& process, const MiddleEndState& state, long syscallNr) override;
};

struct OpenAT : OpenBase {
	void entry(const processState& process, const MiddleEndState& state, long syscallNr) override;
};
struct OpenAT2 : OpenBase {
	void entry(const processState& process, const MiddleEndState& state, long syscallNr) override;
};

HandlerClassDef(SYS_open) : public Open{};
HandlerClassDef(SYS_openat) : public OpenAT{};
HandlerClassDef(SYS_openat2) : public OpenAT2{};

HandlerClassDef(SYS_creat) : public Creat{};
