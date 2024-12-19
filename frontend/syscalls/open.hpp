#pragma once
#include "genericSyscallHeader.hpp"
#include <filesystem>
namespace frontend::SyscallHandlers {
struct OpenBase : simpleSyscallHandler_base {
    void exit(processState& process, middleend::MiddleEndState& state,
              long syscallRetval) override;
    void entryLog(const processState& process,
                  const middleend::MiddleEndState& state,
                  long syscallNr) override;

    void checkIfExists(const processState& process,
                       const middleend::MiddleEndState& middleEnd);

    absFilePath resolvedPath;
    std::filesystem::path fileRelPath;
    long flags;
    fileDescriptor at;
    std::optional<middleend::MiddleEndState::statResults> statResults;
};

struct Open : OpenBase {
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};
struct Creat : OpenBase {
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};

struct OpenAT : OpenBase {
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};
struct OpenAT2 : OpenBase {
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};

HandlerClassDef(SYS_open) : public Open{};
HandlerClassDef(SYS_openat) : public OpenAT{};
HandlerClassDef(SYS_openat2) : public OpenAT2{};

HandlerClassDef(SYS_creat) : public Creat{};
} // namespace frontend::SyscallHandlers
