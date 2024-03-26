#pragma once
#include <unistd.h>
#include <memory>
#include <optional>
#include <unistd.h> //syscall fn
#include <cassert>
#include "middleend/middleEnd.hpp"

//circular dependency hell
struct syscallHandler;

struct processState {
	processState(pid_t pid);
	processState(const processState&) = delete;
	processState(processState&&) = delete;
	pid_t pid;
	fileDescriptor pidFD; //TODO: wrap me in a class.
	fileDescriptor stealFD(fileDescriptor FD) const{ //todo: wrap the resul in a class
		auto ret = syscall(SYS_pidfd_getfd, pidFD, FD,0);
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
};

void ptraceChildren();
