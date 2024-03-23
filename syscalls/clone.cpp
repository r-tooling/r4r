#include "clone.hpp"

void Clone::entry(const processState& process, const MiddleEndState& state, long syscallNr)
{

	/*
* https://github.com/strace/strace/blob/f2ae075f5e3ceff869b37ae633549f3ecf75666f/src/clone.c#L105
* TODO on syscall entry:
* We can clear CLONE_PTRACE here since it is an ancient hack
* to allow us to catch children, and we use another hack for that.
* But CLONE_PTRACE can conceivably be used by malicious programs
* to subvert us. By clearing this bit, we can defend against it:
* in untraced execution, CLONE_PTRACE should have no effect.
*
* We can also clear CLONE_UNTRACED, since it allows to start
* children outside of our control. At the moment
* I'm trying to figure out whether there is a *legitimate*
* use of this flag which we should respect.
*/

	//TODO: clone may terminate later than the cild process did. What shall be done in such a case?
	//TODO: this is very much platform specific
	flags = getSyscallParam<1>(process.pid); //TODO: won't this fail for fork and such?
}

void Clone::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval >= 0) {
		state.trackNewProcess(syscallRetval, process.pid, !(flags & CLONE_FILES));
	}
}

void Clone::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr);
	strBuf << "()";

}
