This folder contains all ptrace-specific parts of the implementation.
All the relevant parts of the code will only interact with the middle end. 
The tracer needs no knowledge of how specifically the dependencies will be gathered. 
Even if the syscall should not continue due to whatever reason untill a dependency is gathered should be passed through the middle end.

Syscalls are handled using dynamic dispatch, though a cast-based solution is easily generatable from my code.

The SyscallHandler class provides the virtual base API. Though the handler implementation is a speciallisation of the  TemplatedSyscallHandler template.
This is done for the ease of creating the SyscallHandlerMapperOfAll class.

When creating a new syscall handler it is included in the template due to template specialisations. But it will need to be included in the 