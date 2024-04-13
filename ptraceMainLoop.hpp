#pragma once
#include <unistd.h>
#include <memory>
#include <optional>
#include <unistd.h> //syscall fn
#include <cassert>
#include "middleend/middleEnd.hpp"
#include "platformSpecificSyscallHandling.hpp"
#include "ptraceHelpers.hpp"
#include "toBeClosedFd.hpp"

//circular dependency hell
struct syscallHandler;

using traceeFileDescriptor = fileDescriptor;

struct processState {
	processState(pid_t pid);
	processState(const processState&) = delete;
	processState(processState&&) = delete;
	pid_t pid;
	ToBeClosedFd pidFD;
	ToBeClosedFd stealFD(traceeFileDescriptor FD) const{
		auto ret = syscall(SYS_pidfd_getfd, pidFD.get(), FD, 0);
		assert(ret >= 0);
		return ret;
	}

	struct ChildWaiting {
		std::optional<pid_t> cloneChildPid = std::nullopt;
		long flags;
	};
	std::optional<ChildWaiting> blockedInClone;

	enum {
		outside,
		inside
	} syscallState = outside;
	std::unique_ptr<syscallHandler> syscallHandlerObj;
	~processState();


	template<int T>
	long getSyscallParam() {
		return ::getSyscallParam<T>(pid);
	}

	std::string ptrToStr(long ptr) {
		return ::userPtrToString(pid, ptr);
	}
};

void ptraceChildren();
