#pragma once
#include "genericSyscallHeader.hpp"
namespace frontend::SyscallHandlers {
HandlerClassDef(SYS_exit_group) : public OnlyEntryLog{};
HandlerClassDef(SYS_exit) : public OnlyEntryLog{};
} // namespace frontend::SyscallHandlers
