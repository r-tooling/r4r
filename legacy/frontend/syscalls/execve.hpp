#pragma once
#include "genericSyscallHeader.hpp"
namespace frontend::SyscallHandlers {
struct Exec : simpleSyscallHandler_base {
    std::filesystem::path fileRelPath;

    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
    void exit(processState& process, middleend::MiddleEndState& state,
              long syscallRetval) override;
    void entryLog(const processState& process,
                  const middleend::MiddleEndState& state,
                  long syscallNr) override;
};

HandlerClassDef(SYS_execve) : public SyscallHandlers::Exec{};
} // namespace frontend::SyscallHandlers
