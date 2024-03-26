#pragma once
#include "genericSyscallHeader.hpp"

namespace SyscallHandlers {

}
HandlerClassDef(SYS_chroot) : public errorHandler{};
HandlerClassDef(SYS_chdir) : public errorHandler{};