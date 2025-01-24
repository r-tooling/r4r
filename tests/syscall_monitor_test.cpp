#include <gtest/gtest.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include "../syscall_monitor.hpp"

// Global test string in the child process.
//
// NOTE: Must be 'static' or 'extern "C"' to avoid name mangling if you
// reference it from outside. Assume no PIE + no ASLR so that the address
// of TEST_STRING in the child is the same as in the parent.
static const char TEST_STRING[] = "Hello from child!";

// Utility function: Fork a child process that contains 'g_test_string' and
// stops itself. Returns child's PID to parent, or throws on failure.
static pid_t fork_and_stop_child() {
    pid_t pid = fork();
    if (pid < 0) {
        throw std::runtime_error("fork() failed: " +
                                 std::string(std::strerror(errno)));
    }

    if (pid == 0) {
        // child
        ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
        raise(SIGSTOP);
        _exit(0);
    } else {
        // parent: wait for child to stop
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            throw std::runtime_error("waitpid() failed: " +
                                     std::string(std::strerror(errno)));
        }
        if (!WIFSTOPPED(status)) {
            throw std::runtime_error("Child did not stop as expected.");
        }
        return pid;
    }
}

TEST(ReadStringFromProcessVm, Full) {
    // assume the same address in child
    auto addr = reinterpret_cast<long>(TEST_STRING);
    auto pid = fork_and_stop_child();
    auto str = SyscallMonitor::read_string_from_process(pid, addr, 1024);

    EXPECT_EQ(str, TEST_STRING);

    ptrace(PTRACE_CONT, pid, nullptr, nullptr);
    waitpid(pid, nullptr, 0);
}

TEST(ReadStringFromProcessVm, Parcial) {
    // assume the same address in child
    auto addr = reinterpret_cast<long>(TEST_STRING);
    pid_t pid = fork_and_stop_child();
    std::string str = SyscallMonitor::read_string_from_process(pid, addr, 5);

    std::string expected = "Hello";
    EXPECT_EQ(str, expected);

    ptrace(PTRACE_CONT, pid, nullptr, nullptr);
    waitpid(pid, nullptr, 0);
}
