#pragma once
#include "genericSyscallHeader.hpp"
#include <variant>
namespace frontend::SyscallHandlers {
struct StatHandler : public simpleSyscallHandler_base {

    fileDescriptor at;
    relFilePath path;
    int flags;
    // todo: I should pass the result to the middle end
    void exit(processState& process, middleend::MiddleEndState& state,
              long syscallRetval) override;
    void entryLog(const processState& process,
                  const middleend::MiddleEndState& state,
                  long syscallNr) override;
};
struct Stat : StatHandler {
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};
struct FStat : StatHandler {
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};
struct LStat : StatHandler {
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};
struct NewFStatAt : StatHandler {
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};
struct StatX : StatHandler {
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};

HandlerClassDef(SYS_stat) : public Stat{};
HandlerClassDef(SYS_fstat) : public FStat{};
HandlerClassDef(SYS_lstat) : public LStat{};
HandlerClassDef(SYS_newfstatat) : public NewFStatAt{};
HandlerClassDef(SYS_statx) : public StatX{};

struct AccessHandler : public simpleSyscallHandler_base {

    fileDescriptor at;
    std::filesystem::path path;
    long flags;
    // todo: do I ever care for the result?
    //  Inherited via simpleSyscallHandler_base
    void exit(processState& process, middleend::MiddleEndState& state,
              long syscallRetval) override;
    void entryLog(const processState& process,
                  const middleend::MiddleEndState& state,
                  long syscallNr) override;
};
struct Access : StatHandler {
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};
struct FAccessAt : StatHandler {
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};
struct FAccessAt2 : StatHandler {
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};
HandlerClassDef(SYS_access) : public Access{};
HandlerClassDef(SYS_faccessat) : public FAccessAt{};
HandlerClassDef(SYS_faccessat2) : public FAccessAt2{};

struct ReadLinkHandler : public simpleSyscallHandler_base {

    fileDescriptor at;
    std::filesystem::path path;
    long userPtr;
    size_t maxBufferSize;
    std::unique_ptr<unsigned char[]> returnedData;
    // todo: mark the relevant aprt in the middle end with info about what this
    // means
    void exit(processState& process, middleend::MiddleEndState& state,
              long syscallRetval) override;
    void entryLog(const processState& process,
                  const middleend::MiddleEndState& state,
                  long syscallNr) override;
    void exitLog(const processState& process,
                 const middleend::MiddleEndState& state,
                 long syscallRetval) override;
};
struct ReadLink : ReadLinkHandler {
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};
struct ReadLinkAt : ReadLinkHandler {
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};
HandlerClassDef(SYS_readlink) : public ReadLink{};
HandlerClassDef(SYS_readlinkat) : public ReadLinkAt{};

// TODO: ustat statfs fstatfs should probably go elsewhere? Stating the
// filesystem, not files
NullOptHandlerClass(SYS_statfs);
NullOptHandlerClass(SYS_fstatfs);
NullOptHandlerClass(SYS_ustat);
} // namespace frontend::SyscallHandlers
