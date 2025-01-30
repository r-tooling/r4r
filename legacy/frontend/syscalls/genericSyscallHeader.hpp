#pragma once
#include "../../middleend/middleEnd.hpp"
#include "../platformSpecificSyscallHandling.hpp"
#include "../ptraceHelpers.hpp"
#include "../syscallMapping.hpp"
#include "./syscallHandler.hpp"

#include <fcntl.h> //AT_*
#include <sstream>
namespace frontend::SyscallHandlers {

struct NullOptHandler : SyscallHandler {
    void entry(processState&, const middleend::MiddleEndState&,
               long) override{};
    void exit(processState&, middleend::MiddleEndState&, long) override{};

    void entryLog(const processState&, const middleend::MiddleEndState&,
                  long) override{};
    void exitLog(const processState&, const middleend::MiddleEndState&,
                 long) override{};
};

struct WarnHandler : NullOptHandler {
    int syscallNr = 0;
    void entry(processState&, const middleend::MiddleEndState&,
               long nr) override {
        syscallNr = nr;
    };
    void exit(processState&, middleend::MiddleEndState& state, long) override {
        std::string call =
            "The entered syscall "; // todo are these allocations needed
        call += getSyscallName(syscallNr).value_or(std::string_view{"Unknown"});
        call += " will result in unhandled nondeterminism";
        state.syscallWarn(syscallNr, call.c_str());
    };
};
struct ErrorHandler : NullOptHandler {
    void entry(processState&, const middleend::MiddleEndState&,
               long syscallNr) override {
        fprintf(stderr, "entering unhandled syscall! %s \n",
                getSyscallName(syscallNr).value_or("Unknown").data());
    };
};

struct simpleSyscallHandler_base : virtual SyscallHandler {
    std::stringstream strBuf; // TODO: could I make do without this member
                              // variable? It is LARGE

    void exitLog(const processState& process, const middleend::MiddleEndState&,
                 long syscallRetval) override {
        auto str = strBuf.str();
        printf("%d: %s = %ld\n", process.pid,
               str.empty() ? "Unknown syscall" : str.c_str(), syscallRetval);
        strBuf.clear();
    }

    void entryLog(const processState&, const middleend::MiddleEndState&,
                  long syscallNr) {
        strBuf << syscallNr << "->"
               << getSyscallName(syscallNr).value_or(
                      std::string_view{"Unknown"})
               << "";
    }

    template <bool log = true>
    void appendResolvedFilename(const processState& process,
                                const middleend::MiddleEndState& state, int fd,
                                std::stringstream& strBuf) {
        if (fd == AT_FDCWD) {
            strBuf << "AT_FDCWD";
        } else if (auto resolved = state.getFilePath<log>(process.pid, fd);
                   resolved.has_value()) {
            strBuf << resolved.value();
        } else {
            strBuf << fd;
        }
    }
};

struct OnlyEntryLog : public simpleSyscallHandler_base {
    void entry(processState&, const middleend::MiddleEndState&,
               long) override{};
    void exit(processState&, middleend::MiddleEndState&, long) override{};

    void entryLog(const processState& process,
                  const middleend::MiddleEndState& state,
                  long syscallNr) override {
        simpleSyscallHandler_base::entryLog(process, state, syscallNr);
        strBuf << "() = exiting\n";
        printf("%d : %s", process.pid, strBuf.str().c_str());
        strBuf.clear();
    };
    void exitLog(const processState&, const middleend::MiddleEndState&,
                 long) override{};
};

struct FileOperationLogger : simpleSyscallHandler_base {
    fileDescriptor fd;
    // Inherited via simpleSyscallHandler_base
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
    void exit(processState& process, middleend::MiddleEndState& state,
              long syscallRetval) override;
    void entryLog(const processState& process,
                  const middleend::MiddleEndState& state,
                  long syscallNr) override;
};

struct PathAtHolder : virtual simpleSyscallHandler_base {
    std::filesystem::path fileRelPath;
    fileDescriptor at;
    void entryLog(const processState& process,
                  const middleend::MiddleEndState& state,
                  long syscallNr) override;
};

/*
        As of now the

*/

template <int nr>
struct TemplatedSyscallHandler;

#define HandlerClassDef(syscallNR)                                             \
    template <>                                                                \
    struct TemplatedSyscallHandler<syscallNR> final

#define NullOptHandlerClass(syscallNR)                                         \
    HandlerClassDef(syscallNR) : public NullOptHandler{};

#define WarnHandlerClass(syscallNR)                                            \
    HandlerClassDef(syscallNR) : public WarnHandler{};
} // namespace frontend::SyscallHandlers

/*intended - macroed - usage
entry{
//huge switch with the following structure:
switch (getSyscallNr()) {
case SYS_open:
//C
process.syscallNr = 1;
assert(process.syscallData == nullptr);
process.syscallData =
TemplatedSyscallHandler<1>::entry(process,state,syscallNr); break;
process.syscallHandlerFn = TemplatedSyscallHandler<1>::exit; break;
process.syscallDeleterFn = TemplatedSyscallHandler<1>::deleter; break;

case SYS_read:

process.syscallNr = 2;
assert(process.syscallData == nullptr);
process.syscallData =
TemplatedSyscallHandler<2>::entry(process,state,syscallNr); break;
process.syscallHandlerFn = TemplatedSyscallHandler<2>::exit; break;
process.syscallDeleterFn = TemplatedSyscallHandler<2>::deleter; break;
//C
process.syscallNr = 1;
assert(process.syscallData == nullptr);
process.syscallData =
TemplatedSyscallHandler<1>::entry(process,state,syscallNr); break;


//C++ like C
process.obj = (void *)new TemplatedSyscallHandler<1>;



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