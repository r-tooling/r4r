#pragma once

#include <sys/ptrace.h>
#include <sys/reg.h>

//TODO: I am platform specific. Wrap me in a header.

/*
* https://www.man7.org/linux/man-pages/man2/syscall.2.html
The first table lists the instruction used to transition to
	   kernel mode (which might not be the fastest or best way to
	   transition to the kernel, so you might have to refer to vdso(7)),
	   the register used to indicate the system call number, the
	   register(s) used to return the system call result, and the
	   register used to signal an error.
		Arch/ABI    Instruction           System  Ret  Ret  Error    Notes
										 call #  val  val2
	   ───────────────────────────────────────────────────────────────────
		 x86-64      syscall               rax     rax  rdx  -        5

		 The second table shows the registers used to pass the system call
	   arguments.
	   Arch/ABI      arg1  arg2  arg3  arg4  arg5  arg6  arg7  Notes
		x86-64        rdi   rsi   rdx   r10   r8    r9    -
*/

inline long getSyscallNr(pid_t processPid) {
	return ptrace(PTRACE_PEEKUSER, processPid, 8 * ORIG_RAX, nullptr);
}
inline long getSyscallRetval(pid_t processPid) {
	/*user_regs_struct regs;
	ptrace(PTRACE_GETREGS, processPid, 0, &regs);
	return regs.rax;*/
	return ptrace(PTRACE_PEEKUSER, processPid, 8 * RAX, nullptr);
}


template<unsigned int NR>
inline long getSyscallParam(pid_t processPid) {//it is impossible to have validation if the given syscall actually has these arguments at this point.
	static_assert(NR >= 1 && NR <= 6, "syscalls only take up to 6 arguments");
	if constexpr (NR == 1) { //would even be consteval in c++ 23
		return ptrace(PTRACE_PEEKUSER, processPid, 8 * RDI, nullptr);
	}
	else if constexpr (NR == 2) {
		return ptrace(PTRACE_PEEKUSER, processPid, 8 * RSI, nullptr);

	}
	else if constexpr (NR == 3) {
		return ptrace(PTRACE_PEEKUSER, processPid, 8 * RDX, nullptr);

	}
	else if constexpr (NR == 4) {
		return ptrace(PTRACE_PEEKUSER, processPid, 8 * R10, nullptr);

	}
	else if constexpr (NR == 5) {
		return ptrace(PTRACE_PEEKUSER, processPid, 8 * R8, nullptr);

	}
	else if constexpr (NR == 6) {
		return ptrace(PTRACE_PEEKUSER, processPid, 8 * R9, nullptr);
	}
	assert(false);//never triggers
}
