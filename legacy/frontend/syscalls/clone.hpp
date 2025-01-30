#pragma once
#include "genericSyscallHeader.hpp"

namespace frontend::SyscallHandlers {
struct Clone : simpleSyscallHandler_base {
    long flags;
    std::optional<long> fileDescriptorPtr = std::nullopt;
    // Inherited via simpleSyscallHandler_base
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
    void handleEntryState(processState& process,
                          const middleend::MiddleEndState& state,
                          long syscallNr);

    void exit(processState& process, middleend::MiddleEndState& state,
              long syscallRetval) override;
    void entryLog(const processState& process,
                  const middleend::MiddleEndState& state,
                  long syscallNr) override;
};

struct Clone3 : Clone {
    // Inherited via simpleSyscallHandler_base
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};

struct Fork : Clone {
    // Inherited via simpleSyscallHandler_base
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};
struct VFork : Clone {
    // Inherited via simpleSyscallHandler_base
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
};
/*		else if (syscallNr == SYS_fork || syscallNr == SYS_vfork ||
   syscallNr == SYS_clone || syscallNr == SYS_clone3) {

                }*/
HandlerClassDef(SYS_clone) : public SyscallHandlers::Clone{};
HandlerClassDef(SYS_clone3) : public SyscallHandlers::Clone3{};

HandlerClassDef(SYS_fork) : public SyscallHandlers::Fork{};
HandlerClassDef(SYS_vfork) : public SyscallHandlers::VFork{};
} // namespace frontend::SyscallHandlers
