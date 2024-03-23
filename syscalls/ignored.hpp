#include "genericSyscallHeader.hpp"

NullOptHandlerClass(SYS_brk)//malloc and such use this. Only changes the memory segments, no real communication
NullOptHandlerClass(SYS_mmap)//does not create new file descriptors or modify the existing ones
NullOptHandlerClass(SYS_arch_prctl) //might be interesting for chacking the mode we are running in - x64 or x32

NullOptHandlerClass(SYS_rt_sigaction) //changes the operatio on a signal, nop cummunication here, really
NullOptHandlerClass(SYS_rt_sigprocmask) //changes the operatio on a signal, nop cummunication here, really

NullOptHandlerClass(SYS_getrusage) //cannot do stuff ofr other processes, unimportant

NullOptHandlerClass(SYS_wait4); //waits for a process, should not be able to wait for a process which is not a child
NullOptHandlerClass(SYS_waitid); //waits for a process, should not be able to wait for a process which is not a child