#include "ptraceMainLoop.hpp"
#include "syscallMapping.hpp"
#include "middleEnd.hpp"
#include "backEnd.hpp"
#include "platformSpecificSyscallHandling.hpp"
#include "ptraceHelpers.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <functional>
#include <optional>
#include <memory>
#include <type_traits>
#include <sstream>

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <linux/openat2.h>
#include <fcntl.h> //open, AT_FDCWD


namespace {

	using namespace std::string_view_literals;



	auto openHandling(pid_t processPid, std::filesystem::path fileRelPath, int flags) {

		//mode is ignored as it is only usefull with O_Creat and the domummentation on it is as follows:
		/*
		Note that mode applies only to future accesses of the
				  newly created file; the open() call that creates a read-
				  only file may well return a read/write file descriptor.
		*/

		return [path = std::move(fileRelPath), flags, processPid](long syscallRetval, MiddleEndState& state) {
			auto FD = static_cast<fileDescriptor>(syscallRetval);
			if (FD < 0) {
				//TODO: log open failed and file is non existent. and the retval
			}
			else {
				std::unique_ptr<char, decltype(std::free)*> resolvedPath{ realpath(path.c_str(), nullptr), std::free };//avoid having to re-implement this in the middle end and possibly add more bugs in the implementation. TODD: handle chroot.
				if (!resolvedPath) {
					printf("Unable to resolve path for file %s\n", path.c_str());
					std::string tmp{ path };
					state.openHandling(processPid, std::move(tmp), std::move(path), FD, flags);//the order is undefined otherwise
				}
				else {
					state.openHandling(processPid, { resolvedPath.get() }, std::move(path), FD, flags);//the order is undefined otherwise
				}
			}
			};
	}


	std::optional<std::function<void(long, MiddleEndState&)>> handleSyscall(pid_t processPid, long syscallNr, const MiddleEndState& state) {

		if (syscallNr == SYS_open) {
			auto fileRelPath = userPtrToString(processPid, getSyscallParam<1>(processPid));
			auto flags = getSyscallParam<2>(processPid);
			return openHandling(processPid, std::move(fileRelPath), flags);
		}
		else if (syscallNr == SYS_openat) {
			/*
			The dirfd argument is used in conjunction with the pathname
		   argument as follows:

		   •  If the pathname given in pathname is absolute, then dirfd is
			  ignored.

		   •  If the pathname given in pathname is relative and dirfd is the
			  special value AT_FDCWD, then pathname is interpreted relative
			  to the current working directory of the calling process (like
			  open()).

		   •  If the pathname given in pathname is relative, then it is
			  interpreted relative to the directory referred to by the file
			  descriptor dirfd (rather than relative to the current working
			  directory of the calling process, as is done by open() for a
			  relative pathname).  In this case, dirfd must be a directory
			  that was opened for reading (O_RDONLY) or using the O_PATH
			  flag.

		   If the pathname given in pathname is relative, and dirfd is not a
		   valid file descriptor, an error (EBADF) results.  (Specifying an
		   invalid file descriptor number in dirfd can be used as a means to
		   ensure that pathname is absolute.)
			*/

			auto fileRelPath = std::filesystem::path{ userPtrToString(processPid, getSyscallParam<2>(processPid)) };
			int dirFd = getSyscallParam<1>(processPid);
			//todo: handle param 1 - I require a reverse FD info lookup.
			assert(dirFd == AT_FDCWD || fileRelPath.is_absolute());
			auto flags = getSyscallParam<3>(processPid);
			return openHandling(processPid, std::move(fileRelPath), flags);
		}
		else if (syscallNr == SYS_openat2) {
			auto structSize = getSyscallParam<4>(processPid);
			assert(structSize == sizeof(open_how));
			auto ptr = getSyscallParam<3>(processPid);
			auto mine = userPtrToOwnPtr(processPid, ptr, structSize);
			auto how = reinterpret_cast<open_how*>(mine.get());

			//todo: support all those magical flags of resolve

			auto fileRelPath = std::filesystem::path{ userPtrToString(processPid, getSyscallParam<2>(processPid)) };
			int dirFd = getSyscallParam<1>(processPid);

			//todo: handle param 1 - I require a reverse FD info lookup.
			assert(dirFd == AT_FDCWD || fileRelPath.is_absolute());
			return openHandling(processPid, std::move(fileRelPath), how->flags);
		}
		else if (syscallNr == SYS_execve) {
			auto fileRelPath = std::filesystem::path{ userPtrToString(processPid, getSyscallParam<1>(processPid)) };
			//we don't really care about the args nor the env. The only env which really matters is the one which is already captured by the ptracer
			return [path = std::move(fileRelPath), processPid](long, MiddleEndState& state) {
				std::unique_ptr<char, decltype(std::free)*> resolvedPath{ realpath(path.c_str(), nullptr), std::free };//avoid having to re-implement this in the middle end and possibly add more bugs in the implementation. TODD: handle chroot.
				if (!resolvedPath) {
					printf("Unable to resolve path for exec %s\n", path.c_str());
					std::string tmp{ path };
					state.execFile(processPid, std::move(tmp), std::move(path));
				}
				else {
					state.execFile(processPid, { resolvedPath.get() }, std::move(path));
				}
				};
		}
		else if (syscallNr == SYS_chroot || syscallNr == SYS_chdir) {
			printf("As of now I do not support usage of a different directory due to issues with realpath resolution"); //TODO: change my cuurent working dir. In the case of a chrooted env, preppend the chroot.
		}
		else if (syscallNr == SYS_close) {
			int dirFd = getSyscallParam<1>(processPid);

			return [processPid, dirFd](long retval, MiddleEndState& state) {
				if (retval == 0) {
					state.closeFile(processPid, dirFd);
				}
				return std::nullopt;
				};
		}
		else if (syscallNr == SYS_read || syscallNr == SYS_pread64) {
			int dirFd = getSyscallParam<1>(processPid);

			return std::nullopt;
		}
		else if (syscallNr == SYS_fork || syscallNr == SYS_vfork || syscallNr == SYS_clone || syscallNr == SYS_clone3) {
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
	 //exit(255);

			//TODO: clone may terminate later than the cild process did. What shall be done in such a case?
			//TODO: this is very much platform specific
			auto flags = getSyscallParam<1>(processPid);
			return [processPid, flags](long retval, MiddleEndState& state) {
				if (retval >= 0) {
					state.trackNewProcess(retval, processPid,!( flags & CLONE_FILES));
				}
				};
		}
		else if (syscallNr == SYS_dup) {
			int dirFd = getSyscallParam<1>(processPid);

			return [processPid, dirFd](long retval, MiddleEndState& state) {
				if (retval != -1) {
					state.registerFdAlias(processPid, retval, dirFd);
				}
				};
		}
		else if (syscallNr == SYS_dup2 || syscallNr == SYS_dup3) {
			int oldFd = getSyscallParam<1>(processPid);
			int newFd = getSyscallParam<2>(processPid);

			//TODO: stop ignoring flags

			return [processPid, oldFd, newFd](long retval, MiddleEndState& state) {
				if (retval != -1) {
					state.registerFdAlias(processPid, newFd, oldFd);
				}
				};
		}
		else if (syscallNr == SYS_pipe || syscallNr == SYS_pipe2) {
			auto pipesPtr = getSyscallParam<1>(processPid);

			return [processPid, pipesPtr](long retval, MiddleEndState& state) {
				if (retval == 0) {
					auto pipes = userPtrToOwnPtr<fileDescriptor, 2>(processPid, pipesPtr);
					state.registerPipe(processPid, pipes.get());
				}
				};
		}
		return std::nullopt;
	}

	struct processState {
		processState(pid_t pid) :pid(pid) {};
		processState(const processState&) = delete;
		processState(processState&&) = delete;
		bool terminated = false;
		pid_t pid;
		enum {
			outside,
			inside
		} syscallState = outside;
		std::invoke_result_t<decltype(handleSyscall), int, long, MiddleEndState> syscallHandlerFn = std::nullopt;
		std::optional<std::string> syscallInfo;
	};







	void appendResolvedFilename(const MiddleEndState& state, pid_t processPid, int fd, std::stringstream& strBuf)
	{
		if (auto resolved = state.getFilePath(processPid, fd); resolved.has_value()) {
			strBuf << resolved.value();
		}
		else {
			strBuf << fd;
		}
	}

	static void logSyscallEntry(processState& process, long syscallNr, const MiddleEndState& state)
	{
		auto processPid = process.pid;
		std::stringstream strBuf;
		strBuf << syscallNr << " ->"sv;
		auto syscallName = getSyscallName(syscallNr);
		if (syscallName) {
			auto view = syscallName.value();
			strBuf << view;
		}
		else {
			strBuf << "Unknown ->"sv;
		}
		if (syscallNr == SYS_open) {
			auto fileRelPath = userPtrToString(processPid, getSyscallParam<1>(processPid));
			strBuf << "("sv;
			strBuf << fileRelPath;
			strBuf << ")"sv;
		}
		else if (syscallNr == SYS_openat) {
			auto fileRelPath = std::filesystem::path{ userPtrToString(processPid, getSyscallParam<2>(processPid)) };
			strBuf << "("sv;
			strBuf << fileRelPath;
			strBuf << ")"sv;
		}
		else if (syscallNr == SYS_openat2) {
			auto fileRelPath = std::filesystem::path{ userPtrToString(processPid, getSyscallParam<2>(processPid)) };
			strBuf << "("sv;
			strBuf << fileRelPath;
			strBuf << ")"sv;
		}
		else if (syscallNr == SYS_read || syscallNr == SYS_write || syscallNr == SYS_pread64) {
			int fd = getSyscallParam<1>(processPid);
			strBuf << "("sv;
			appendResolvedFilename(state, processPid, fd, strBuf);
			strBuf << ")"sv;
		}
		else if (syscallNr == SYS_close) {
			int fd = getSyscallParam<1>(processPid);
			strBuf << "("sv;
			appendResolvedFilename(state, processPid, fd, strBuf);
			strBuf << ")"sv;
		}
		else if (syscallNr == SYS_fork || syscallNr == SYS_vfork || syscallNr == SYS_clone || syscallNr == SYS_clone3) {
		}
		else if (syscallNr == SYS_dup) {
			int oldFd = getSyscallParam<1>(processPid);
			strBuf << "(retval = "sv;
			appendResolvedFilename(state, processPid, oldFd, strBuf);
			strBuf << ")"sv;
		}
		else if (syscallNr == SYS_dup2 || syscallNr == SYS_dup3) {
			int oldFd = getSyscallParam<1>(processPid);
			int newFd = getSyscallParam<2>(processPid);
			strBuf << "( "sv << newFd << "= "sv;
			appendResolvedFilename(state, processPid, oldFd, strBuf);
			strBuf << ")"sv;
		}
		else if (syscallNr == SYS_exit_group || syscallNr == SYS_exit) {
			strBuf << "() = exiting\n"sv;
		}
		else if (syscallNr == SYS_execve) {
			//handled.
		}
		else if (syscallNr == SYS_brk) {
			//intentionally ignored
			//brk - basically mallocking stuff
		}
		else if (syscallNr == SYS_mmap || syscallNr == SYS_arch_prctl) {
			//does not create new file descriptors or modify the existing ones
		}
		else {
			/*printf("please add handling for syscall %s\n", syscallName->data());
			assert(false);*/
		}
		//TODO: handle pipes
		process.syscallInfo = strBuf.str();
	}

	void logSyscallExit(processState& process, long val) {
		printf("%d: %s = %ld\n", process.pid, process.syscallInfo.value_or(std::string("Unknown syscall")).c_str(), val);
		process.syscallInfo = std::nullopt;
	}

	void logSyscallExit(processState& process) {
		printf("%d: %s\n", process.pid, process.syscallInfo.value_or(std::string("Unknown syscall")).c_str());
		process.syscallInfo = std::nullopt;
	}

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
		state.trackNewProcess(process.pid);
		return process;
	}
	void removeProcessHandling(processState& process) {
		if (process.syscallInfo) {
			logSyscallExit(process);
		}
		processing.erase(process.pid);
	}
	bool processsesLeft() {
		return !processing.empty();
	}
	bool tryWait(siginfo_t& status) {
		int waitStatus;
		while (true) {
			waitStatus = waitid(P_ALL, 0, &status, WSTOPPED | WCONTINUED | WEXITED); //tracing all children, they SHOULD be ptraced, but hey, what if they are not? In that case we at least get a ptrace error down the line.
			if (waitStatus == -1) {
				auto waitError = errno;//todo: check that this is due to no more children
				return false;
			}
			assert(waitStatus == 0);
			//TODO: add qquick cases for when we just proceed to wait as this is not a relevant handling point.
			break;
		}
		return true;
	}
}





void ptraceChildren()
{
	MiddleEndState state{};
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
					if (process.syscallHandlerFn.has_value()) {
						process.syscallHandlerFn.value()(val, state);
					}
					process.syscallState = processState::outside;
				}
				else if (process.syscallState == processState::outside) {
					if (getSyscallRetval(process.pid) != -38) {//syscall entry TODO: this is platform specific!!!!
						assert(false); //i was not a syscall or was not syscall entry point.
					};
					long syscall_id = getSyscallNr(process.pid);
					process.syscallHandlerFn = handleSyscall(process.pid, syscall_id, state);

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
	csvBased(state, "accessedFiles.csv");
	report(state);
	//chrootBased(state);
}

