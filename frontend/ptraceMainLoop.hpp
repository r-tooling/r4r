#pragma once
#include <unistd.h>
#include <memory>
#include <optional>
#include <unistd.h> //syscall fn
#include <cassert>
#include "../middleend/middleEnd.hpp"
#include "platformSpecificSyscallHandling.hpp"
#include "ptraceHelpers.hpp"
#include "../toBeClosedFd.hpp"

namespace frontend{

	//circular dependency hell resolution
	namespace SyscallHandlers{
		struct SyscallHandler;
	}

	using traceeFileDescriptor = fileDescriptor;
	/*
		The state of the process and relevant information for the tracer, basically clones what the userspace knows.
	*/
	struct processState {
		processState(pid_t pid);
		processState(const processState&) = delete;
		processState(processState&&) = delete;
		pid_t pid;
		/*ToBeClosedFd pidFD;
		
			While a decent idea, I could not consistently get PID fds for threads
		ToBeClosedFd stealFD(traceeFileDescriptor FD) const {
			auto ret = syscall(SYS_pidfd_getfd, pidFD.get(), FD, 0);
			assert(ret >= 0);
			return ToBeClosedFd{ static_cast<fileDescriptor>(ret) };
		}*/

		struct ChildWaiting {
			std::optional<pid_t> cloneChildPid = std::nullopt;
			long flags;
		};
		std::optional<ChildWaiting> blockedInClone;

		enum {
			outside,
			inside
		} syscallState = outside;
		std::unique_ptr<SyscallHandlers::SyscallHandler> syscallHandlerObj;
		~processState();


		template<int T>
		long getSyscallParam() {
			return frontend::getSyscallParam<T>(pid);
		}

		std::string ptrToStr(long ptr) {
			return frontend::userPtrToString(pid, ptr);
		}
	};
	/*
	* Will wait untill all children have terminated
	* Assumes all children are ptraced.
	*/
	void ptraceChildren(middleend::MiddleEndState& state);
}