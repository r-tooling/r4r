#pragma once
#include "genericSyscallHeader.hpp"
#include <filesystem>
namespace frontend::SyscallHandlers {
struct MkdirBase : simpleSyscallHandler_base {
    void exit(processState& process, middleend::MiddleEndState& state,
              long syscallRetval) override;
    void entryLog(const processState& process,
                  const middleend::MiddleEndState& state,
                  long syscallNr) override;

    std::filesystem::path fileRelPath;
    long flags;
    fileDescriptor at;
    bool existed;
};

struct Mkdir : MkdirBase {
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};
struct MkdirAt : MkdirBase {
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};

HandlerClassDef(SYS_mkdir) : public Mkdir{};
HandlerClassDef(SYS_mkdirat) : public MkdirAt{};
} // namespace frontend::SyscallHandlers