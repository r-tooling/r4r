#pragma once
#include "genericSyscallHeader.hpp"

HandlerClassDef(SYS_exit_group) : public SyscallHandlers::onlyEntryLog{};
HandlerClassDef(SYS_exit) : public SyscallHandlers::onlyEntryLog{};

