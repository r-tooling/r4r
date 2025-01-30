#include <fcntl.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include "syscall_monitor.h"

// Global test string in the child process.
//
// NOTE: Must be 'static' or 'extern "C"' to avoid name mangling if you
// reference it from outside. Assume no PIE + no ASLR so that the address
// of TEST_STRING in the child is the same as in the parent.
static char const TEST_STRING[] = "Hello from child!";

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

class TestSyscallListener : public SyscallListener {
  public:
    void on_syscall_entry(pid_t pid, std::uint64_t syscall,
                          SyscallArgs args) override {
        entries.push_back({syscall});
        state.insert_or_assign(pid, syscall);
    }

    void on_syscall_exit(pid_t pid, SyscallRet ret, bool is_error) override {
        exits.push_back({state[pid]});
    }

    struct EntryCall {
        std::uint64_t syscall;
    };

    struct ExitCall {
        std::uint64_t syscall;
    };

    std::vector<EntryCall> entries;
    std::vector<ExitCall> exits;
    std::unordered_map<pid_t, std::uint64_t> state;
};

TEST(SyscallMonitorTest, OpenSyscall) {
    fs::path temp_file = create_temp_file("r4r-test", ".delete");

    ASSERT_FALSE(fs::exists(temp_file));

    auto tracee = [file = temp_file.c_str()]() {
        int fd = ::open(file, O_CREAT | O_WRONLY, 0644);
        return fd > 0 ? 0 : fd;
    };

    TestSyscallListener listener;
    SyscallMonitor monitor{tracee, listener};

    auto result = monitor.start();
    ASSERT_EQ(result.kind, SyscallMonitor::Result::Exit);
    ASSERT_EQ(result.detail, 0);

    ASSERT_EQ(listener.entries.size(), 2); // + 1 for exit
    ASSERT_EQ(listener.exits.size(), 1);
    ASSERT_EQ(listener.entries[0].syscall, SYS_openat);
    ASSERT_EQ(listener.exits[0].syscall, SYS_openat);

    ASSERT_TRUE(fs::exists(temp_file));
    ASSERT_TRUE(fs::remove(temp_file));
}

TEST(SyscallMonitorTest, CreatSyscall) {
    // creat is essentially open(..., O_CREAT|O_WRONLY, mode)
    // and on linux, it is implemented using open,
    // but it should have it own syscall number
    // this test just proves that we need to trace all syscalls dealing with
    // files
    fs::path temp_file = create_temp_file("r4r-test", ".delete");

    ASSERT_FALSE(fs::exists(temp_file));

    auto tracee = [file = temp_file.c_str()]() {
        int fd = ::creat(file, 0644);
        return fd > 0 ? 0 : fd;
    };

    TestSyscallListener listener;
    SyscallMonitor monitor{tracee, listener};

    auto result = monitor.start();
    ASSERT_EQ(result.kind, SyscallMonitor::Result::Exit);
    ASSERT_EQ(result.detail, 0);

    ASSERT_EQ(listener.entries.size(), 2); // + 1 for exit
    ASSERT_EQ(listener.exits.size(), 1);
    ASSERT_EQ(listener.entries[0].syscall, SYS_creat);
    ASSERT_EQ(listener.exits[0].syscall, SYS_creat);

    ASSERT_TRUE(fs::exists(temp_file));
    ASSERT_TRUE(fs::remove(temp_file));
}

TEST(SyscallMonitorTest, OpenSyscallFromChild) {
    fs::path temp_file = create_temp_file("r4r-test-child", ".delete");

    ASSERT_FALSE(fs::exists(temp_file));

    auto tracee = [file = temp_file.c_str()]() -> int {
        pid_t pid = ::fork();
        EXPECT_GE(pid, 0);

        if (pid == 0) {
            int fd = open(file, O_CREAT | O_WRONLY, 0644);
            exit(fd > 0 ? 0 : fd); // child must exit
        } else {
            int status = 0;
            waitpid(pid, &status, 0);
            EXPECT_TRUE(WIFEXITED(status));
            EXPECT_EQ(WEXITSTATUS(status), 0);
        }
        return 0;
    };

    TestSyscallListener listener;
    SyscallMonitor monitor{tracee, listener};

    auto result = monitor.start();
    ASSERT_EQ(result.kind, SyscallMonitor::Result::Exit);
    ASSERT_EQ(result.detail, 0);

    int open_at_calls = 0;
    for (auto& entry : listener.entries) {
        if (entry.syscall == SYS_openat) {
            open_at_calls += 1;
        }
    }
    ASSERT_EQ(open_at_calls, 1);

    open_at_calls = 0;
    for (auto& exit : listener.exits) {
        if (exit.syscall == SYS_openat) {
            open_at_calls += 1;
        }
    }
    ASSERT_EQ(open_at_calls, 1);

    ASSERT_TRUE(fs::exists(temp_file));
    ASSERT_TRUE(fs::remove(temp_file));
}
