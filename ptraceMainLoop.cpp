#include "ptraceMainLoop.hpp"
#include "syscallMapping.hpp"
#include "platformSpecificSyscallHandling.hpp"
#include "ptraceHelpers.hpp"
#include "syscalls/syscallHandlerMapper.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <memory>
#include <type_traits>

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <fcntl.h> //open, AT_FDCWD


namespace {

	using namespace std::string_view_literals;


	static std::unordered_map<pid_t, processState> processing;//TODO: move this so it is not global but passed as an argument.

	processState& getProcesState(pid_t pid, MiddleEndState& state) {
		auto found = processing.find(pid);
		if (found != processing.end()) {
			return found->second;
		}
		auto emplaced = processing.emplace(pid,pid);
		assert(emplaced.second);
		auto& process = emplaced.first->second;
		ptrace(PTRACE_SETOPTIONS, process.pid, nullptr, PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK | PTRACE_O_TRACECLONE | PTRACE_O_TRACESYSGOOD);

		for (auto& [pid,oldProcess] : processing) { 
			//todo: try to walk all currently blocked processes to get this marked down
			//todo: consider keeping the list in a separate structure.
			if (oldProcess.blockedInClone && !oldProcess.blockedInClone->cloneChildPid.has_value()) {
				state.trackNewProcess(process.pid, oldProcess.pid, !(process.blockedInClone->flags & CLONE_FILES), std::nullopt, process.blockedInClone->flags & CLONE_FS);
				oldProcess.blockedInClone->cloneChildPid = process.pid;
				return process;
			}
		}
		state.trackNewProcess(process.pid);
		return process;
	}
	void removeProcessHandling(processState& process) {
		processing.erase(process.pid);
	}

	bool tryWait(siginfo_t& status) {
		int waitStatus;
		while (true) {
			waitStatus = waitid(P_ALL, 0, &status, WSTOPPED | WCONTINUED | WEXITED); //tracing all children, they SHOULD be ptraced, but hey, what if they are not? In that case we at least get a ptrace error down the line.
			if (waitStatus == -1) {
				auto waitError = errno;//todo: check that this is due to no more children
				assert(waitError == ECHILD);
				return false;
			}
			assert(waitStatus == 0);
			//TODO: add qquick cases for when we just proceed to wait as this is not a relevant handling point.
			break;
		}
		return true;
	}
}





void ptraceChildren(MiddleEndState& state)
{
	static const syscallHandlerMapperOfAll mapper{};
	siginfo_t status;
	while (tryWait(status)) {
		bool doPtrace = true;
		{
			auto& process = getProcesState(status.si_pid, state);
			switch (status.si_code)
			{
			case CLD_EXITED:
			case CLD_KILLED:
			case CLD_DUMPED:
				state.toBeDeleted(process.pid);
				//does not delete as we may have cought the parent exit before the child exec and we need the FD table to be shared properly. Will error on any more syscalls but not error on trace new with the same PID
				removeProcessHandling(process);
				doPtrace = false;
				break;
			case CLD_TRAPPED: //(traced child has trapped);
				if (status.si_status != (SIGTRAP | 0x80)) {
					break;//manual sigtrap
				}
				if (process.syscallState == processState::inside) {
					long val = getSyscallRetval(process.pid);
					//logSyscallExit(process, val);
					if (process.syscallHandlerObj) {
						process.syscallHandlerObj->exit(process, state, val);
						process.syscallHandlerObj->exitLog(process,state,val);
						process.syscallHandlerObj = nullptr;
					}
					process.syscallState = processState::outside;
				}
				else if (process.syscallState == processState::outside) {
					if (getSyscallRetval(process.pid) != -ENOSYS) {//syscall entry TODO: this is platform specific!!!!
						assert(false); //i was not a syscall or was not syscall entry point.
					};
					long syscall_id = getSyscallNr(process.pid);
					auto ptr = mapper.get(syscall_id);
					ptr->entry(process, state, syscall_id);
					ptr->entryLog(process, state, syscall_id);
					process.syscallHandlerObj = std::move(ptr);
					//logSyscallEntry(process, syscall_id, state);
					process.syscallState = processState::inside;
				}

			case CLD_STOPPED: //(child stopped by signal); - dont care unless we add a state where the child is stopped
			case CLD_CONTINUED: //(child continued by SIGCONT) - dont care
				break;
			default:
				assert(false);
				break;
			}
		}//the process object may be currently invalid
		if (doPtrace) { 
			if (ptrace(PTRACE_SYSCALL, status.si_pid, nullptr, nullptr) != 0) {
				//TODO: check that this is because all children have quit;
				//the loop itself will terminate due to wait.
			}
		}
	}
}
//intentionally here this way as otherwise the unique_ptr will not compile
processState::processState(pid_t pid) :pid(pid),pidFD(-1) {
	pidFD.reset(static_cast<fileDescriptor>(syscall(SYS_pidfd_open, pid, 0))); //cannot default init as that results in invalid errno
	auto err = errno; 
	if (!pidFD) { //error state
		fprintf(stderr, "pidfd_open(%d): ",pid);
		switch (err)
		{
		case EINVAL:
			fprintf(stderr, "pid is not valid.(or flags but that is impossible) \n");
			break;
		case EMFILE:
			fprintf(stderr, "The per-process limit on the number of open file descriptors has been reached\n");
			break;
		case ENFILE:
			fprintf(stderr, "The system-wide limit on the total number of open fileshas been reached.\n");
			break;
		case ENODEV:
			fprintf(stderr, "The anonymous inode filesystem is not available in this kernel.\n");
			break;
		case ENOMEM:
			fprintf(stderr, "Insufficient kernel memory was available.\n");
			break;
		case ESRCH:
			fprintf(stderr, "The process specified by pid does not exist.\n");
			//todo: recover?
			assert(false);
			break;
		case ENOSYS:
			fprintf(stderr, "The kernel you are using does not have this syscall.\n");
			//todo: recover?
			assert(false);
			break;
		default:
			fprintf(stderr, "Unknown error: %d\n", err);
			assert(false); //todo; handle unknown error
			break;
		}
	}

};
processState::~processState(){}
