#pragma once
#include "genericSyscallHeader.hpp"

HandlerClassDef(SYS_chroot) : public errorHandler{};
HandlerClassDef(SYS_chdir) : public errorHandler{};
