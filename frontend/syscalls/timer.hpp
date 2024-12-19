#pragma once
#include "./genericSyscallHeader.hpp"

// timerfd_create
namespace frontend::SyscallHandlers {

struct TimerfdCreate : simpleSyscallHandler_base {
    void entry(processState& process, const middleend::MiddleEndState& state,
               long syscallNr) override;
    void exit(processState& process, middleend::MiddleEndState& state,
              long syscallRetval) override;
    void entryLog(const processState& process,
                  const middleend::MiddleEndState& state,
                  long syscallNr) override;
};

HandlerClassDef(SYS_timerfd_create) : public TimerfdCreate{};
NullOptHandlerClass(SYS_timerfd_settime) // TODO: check integrity
} // namespace frontend::SyscallHandlers