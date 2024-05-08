#include "clone.hpp"
#include <cassert>

#include <linux/sched.h>    /* Definition of struct clone_args */
#include <sched.h>          /* Definition of CLONE_* constants */
#include <unistd.h>

static_assert(getBuild() == std::string_view{ "x86_64" }, "The clone interfaces may differ on different plaforms");
namespace frontend::SyscallHandlers {


	void Clone::entry(processState& process, const middleend::MiddleEndState& state, long syscallNr)
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
		handleEntryState(process, state, syscallNr);
	}

	void Clone::handleEntryState(processState& process, const middleend::MiddleEndState&, long)
	{
		process.blockedInClone = { std::nullopt, flags }; //TODO: maybe just save &this?
	}

	void Clone::exit(processState& process, middleend::MiddleEndState& state, long syscallRetval)
	{
		assert(process.blockedInClone);//can be safely removed later on.
		if (syscallRetval >= 0) {
			state.trackNewProcess(syscallRetval, process.pid, !(process.blockedInClone->flags & CLONE_FILES), process.blockedInClone->cloneChildPid, process.blockedInClone->flags & CLONE_FS);
			if (fileDescriptorPtr && fileDescriptorPtr.value() != 0) {
				auto ptr = userPtrToOwnPtr<pid_t>(process.pid, fileDescriptorPtr.value());
				state.registerProcessFD(process.pid, syscallRetval, *ptr);
			}
		}
		else {
			assert(!process.blockedInClone || !process.blockedInClone->cloneChildPid.has_value());
		}

		process.blockedInClone = std::nullopt;
	}

	void Clone::entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr)
	{
		simpleSyscallHandler_base::entryLog(process, state, syscallNr);
		strBuf << "()";

	}

	void Clone3::entry(processState& process, const middleend::MiddleEndState& state, long syscallNr)
	{
		size_t size = getSyscallParam<2>(process.pid);
		(void)size;
		assert(size == sizeof(clone_args));
		auto data = userPtrToOwnPtr<clone_args>(process.pid, getSyscallParam<1>(process.pid));
		flags = data->flags & ~0xff;
		fileDescriptorPtr = data->pidfd;
		handleEntryState(process, state, syscallNr);
	}

	void Fork::entry(processState& process, const middleend::MiddleEndState& state, long syscallNr)
	{
		/*
		* https://github.com/torvalds/linux/blob/70293240c5ce675a67bfc48f419b093023b862b3/kernel/fork.c#L2881C2-L2884C1
	struct kernel_clone_args args = {
		.exit_signal = SIGCHLD,
	};

	*/
		flags = 0;
		handleEntryState(process, state, syscallNr);

	}

	void VFork::entry(processState& process, const middleend::MiddleEndState& state, long syscallNr)
	{
		/*	struct kernel_clone_args args = {
			.flags		= CLONE_VFORK | CLONE_VM,
			.exit_signal	= SIGCHLD,
		};*/
		flags = CLONE_VFORK | CLONE_VM;
		handleEntryState(process, state, syscallNr);
	}
}