#pragma once
#include "genericSyscallHeader.hpp"

namespace frontend::SyscallHandlers {
struct Event : simpleSyscallHandler_base {
    void entry(processState&, const middleend::MiddleEndState&,
               long) override{};
    void exit(processState& process, middleend::MiddleEndState& state,
              long syscallRetval) override;
    void entryLog(const processState& process,
                  const middleend::MiddleEndState& state,
                  long syscallNr) override;
};
HandlerClassDef(SYS_eventfd)
    : public SyscallHandlers::Event{}; // TODO: close on exec
HandlerClassDef(SYS_eventfd2) : public SyscallHandlers::Event{};
} // namespace frontend::SyscallHandlers
