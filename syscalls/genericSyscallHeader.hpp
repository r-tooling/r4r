#pragma once
#include "../middleEnd.hpp"
#include "../platformSpecificSyscallHandling.hpp"
#include "../ptraceHelpers.hpp"
#include "../syscallMapping.hpp"
#include "./syscallHandler.hpp"

#include <sstream>


struct nullOptHandler :syscallHandler {
	void entry(const processState& process, const MiddleEndState& state, long syscallNr) override {};
	void exit(processState& process, MiddleEndState& state, long syscallRetval) override {};

	void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) override {};
	void exitLog(const processState& process, const MiddleEndState& state, long syscallRetval) override {};
};

struct errorHandler :nullOptHandler {
	void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) {
		fprintf(stderr, "entering unhandled syscall! %s \n", getSyscallName(syscallNr).value_or("Unknown").data());
	};
};


struct simpleSyscallHandler_base : virtual syscallHandler {
	std::stringstream strBuf; //TODO: could I make do without this member variable? It is LARGE

	void exitLog(const processState& process, const MiddleEndState& state, long syscallRetval) override
	{
		auto str = strBuf.str();
		printf("%d: %s = %ld\n", process.pid, str.empty() ? "Unknown syscall" : str.c_str(), syscallRetval);
		strBuf.clear();
	}

	void entryLog(const processState& process, const MiddleEndState& state, long syscallNr) {
		strBuf << syscallNr << "->" << getSyscallName(syscallNr).value_or(std::string_view{ "Unknown" }) << "";
	}

	void appendResolvedFilename(const processState& process, const MiddleEndState& state, int fd, std::stringstream& strBuf)
	{
		if (auto resolved = state.getFilePath(process.pid, fd); resolved.has_value()) {
			strBuf << resolved.value();
		}
		else {
			fprintf(stderr, "Unable to resolve file descriptor %d", fd);
			strBuf << fd;
		}
	}
};

template<int nr>
struct simpleSyscallHandler;

#define HandlerClass(syscallNR) \
template<> \
struct simpleSyscallHandler<syscallNR> : virtual public syscallHandler
#define HandlerClassDef(syscallNR) \
template<> \
struct simpleSyscallHandler<syscallNR>
#define NullOptHandlerClass(syscallNR)  HandlerClassDef(syscallNR) :public nullOptHandler{};


	/*intended - macroed - usage
entry{
//huge switch with the following structure:
switch (getSyscallNr()) {
case SYS_open:
//C
	process.syscallNr = 1;
	assert(process.syscallData == nullptr);
	process.syscallData = simpleSyscallHandler<1>::entry(process,state,syscallNr); break;
	process.syscallHandlerFn = simpleSyscallHandler<1>::exit; break;
	process.syscallDeleterFn = simpleSyscallHandler<1>::deleter; break;

case SYS_read:

	process.syscallNr = 2;
	assert(process.syscallData == nullptr);
	process.syscallData = simpleSyscallHandler<2>::entry(process,state,syscallNr); break;
	process.syscallHandlerFn = simpleSyscallHandler<2>::exit; break;
	process.syscallDeleterFn = simpleSyscallHandler<2>::deleter; break;
//C 
process.syscallNr = 1;
	assert(process.syscallData == nullptr);
	process.syscallData = simpleSyscallHandler<1>::entry(process,state,syscallNr); break;


//C++ like C
	process.obj = (void *)new simpleSyscallHandler<1>;



//C++ dyn dispatch
case 1:
if(process.obj){    delete process.obr; process.obj = nullptr;}
decltype(process.obj) == struct MY_GENERIC_HADLER;
process.obj = new simpleSyscallHandler_1;
	// dìdí z HANDLER
	break;

process.obj->entry();
} 

//handlerDef.h
#include "open.hpp" //open, openat openat2
//struct simpleSyscallHandle_1{}
#include "close.hpp"

abstract struct GENERIC{
	virtual ~GENERIC();
}

template<>
struct simpleSyscallHandler<1>:GENERIC{	
}

template<int nr, ...>
class dispatch : dispatch<...>{
	construct(){
		map.emplace(nr, simpleSyscallHandler<nr>);
	}
}


static std::unsorted_map<REG_SIZE,MY_GENERIC_HADLER()> map{
{1, ()[]{return new simpleSyscallHandler_1}},
...,

}
process.obj = map.get(nr)(); //handle errror

*/