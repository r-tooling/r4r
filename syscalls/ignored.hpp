#include "genericSyscallHeader.hpp"

NullOptHandlerClass(SYS_brk)//malloc and such use this. Only changes the memory segments, no real communication
NullOptHandlerClass(SYS_mmap)//does not create new file descriptors or modify the existing ones. 
NullOptHandlerClass(SYS_munmap)//does not create new file descriptors or modify the existing ones. 
NullOptHandlerClass(SYS_arch_prctl) //might be interesting for chacking the mode we are running in - x64 or x32

NullOptHandlerClass(SYS_rt_sigaction) //changes the operatio on a signal, nop cummunication here, really
NullOptHandlerClass(SYS_rt_sigprocmask) //changes the operatio on a signal, nop cummunication here, really

NullOptHandlerClass(SYS_getrusage) //cannot do stuff ofr other processes, unimportant

NullOptHandlerClass(SYS_wait4); //waits for a process, should not be able to wait for a process which is not a child
NullOptHandlerClass(SYS_waitid); //waits for a process, should not be able to wait for a process which is not a child

NullOptHandlerClass(SYS_mprotect) //just protection keys for memory regions, tiself will not cause communication

NullOptHandlerClass(SYS_getrandom) //TODO: this is called in the process init. war if it is ever sent otherwise.

NullOptHandlerClass(SYS_getpid) //TODO: warn
NullOptHandlerClass(SYS_getppid)

NullOptHandlerClass(SYS_sysinfo) //TODO: warn, also could be used for data about the current system
NullOptHandlerClass(SYS_uname)

NullOptHandlerClass(SYS_getrlimit) //dont care
NullOptHandlerClass(SYS_setrlimit) //just for itself, not an issue
NullOptHandlerClass(SYS_prlimit64) //TODO: consider the PID implications


NullOptHandlerClass(SYS_getuid) //TODO: if these exist, the current UID/GUID and such may be relevant for what needs to be moved over to the other system.
NullOptHandlerClass(SYS_getgid) //TODO: so maybe do a logger here?
NullOptHandlerClass(SYS_setuid)
NullOptHandlerClass(SYS_setgid)
NullOptHandlerClass(SYS_setpgid)
NullOptHandlerClass(SYS_getpgid)
NullOptHandlerClass(SYS_getpgrp) //no set counterpart???
NullOptHandlerClass(SYS_geteuid)
NullOptHandlerClass(SYS_getegid)
NullOptHandlerClass(SYS_setreuid)
NullOptHandlerClass(SYS_setregid)
NullOptHandlerClass(SYS_setresuid)
NullOptHandlerClass(SYS_setresgid)

NullOptHandlerClass(SYS_set_tid_address); //could potentially be used for getting a tid value and what not.

NullOptHandlerClass(SYS_madvise)
NullOptHandlerClass(SYS_sched_getaffinity)

NullOptHandlerClass(SYS_sigaltstack) //should not affect us in any way