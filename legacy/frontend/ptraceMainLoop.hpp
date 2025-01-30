#pragma once
#include "../middleend/middleEnd.hpp"
#include "../toBeClosedFd.hpp"
#include "platformSpecificSyscallHandling.hpp"
#include "ptraceHelpers.hpp"
#include <cassert>
#include <memory>
#include <optional>
#include <string_view>
#include <unistd.h>
#include <unistd.h> //syscall fn
#include <variant>

#include "sys/user.h"

namespace frontend {

namespace SyscallHandlers {
struct HandlerWrapper;
};

using traceeFileDescriptor = fileDescriptor;
/*
        The state of the process and relevant information for the tracer,
   basically clones what the userspace knows.
*/
struct processState {
    processState(pid_t pid);
    processState(const processState&) = delete;
    processState(processState&&) = delete;
    pid_t pid;
    /*ToBeClosedFd pidFD;

            While a decent idea, I could not consistently get PID fds for
    threads ToBeClosedFd stealFD(traceeFileDescriptor FD) const { auto ret =
    syscall(SYS_pidfd_getfd, pidFD.get(), FD, 0); assert(ret >= 0); return
    ToBeClosedFd{ static_cast<fileDescriptor>(ret) };
    }*/

    struct ChildWaiting {
        std::optional<pid_t> cloneChildPid = std::nullopt;
        long flags;
    };
    std::optional<ChildWaiting> blockedInClone;

    enum { outside, inside } syscallState = outside;
    std::unique_ptr<frontend::SyscallHandlers::HandlerWrapper>
        syscallHandlerObj; // needs to be a pointer to avoid dependency hell.
    ~processState();

    template <int T>
    long getSyscallParam() {
        return frontend::getSyscallParam<T>(pid);
    }
    template <class result = std::string>
    result ptrToStr(long ptr) {
        return frontend::userPtrToString(pid, ptr);
    }
};

class TraceResult {
  public:
    const enum Kind { Exit, Signal } kind;

    TraceResult(Kind kind, int payload) : kind{kind}, payload_{payload} {}

    int exit_code() const {
        assert(kind == Exit);
        return payload_;
    }

    int signal() const {
        assert(kind == Signal);
        return payload_;
    }

    bool is_successfull() const { return kind == Exit && payload_ == 0; }

  private:
    const int payload_;
};

TraceResult trace(middleend::MiddleEndState& state, bool logSyscalls = false);
} // namespace frontend
