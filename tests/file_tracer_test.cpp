#include "file_tracer.h"
#include "syscall_monitor.h"
#include "util_fs.h"
#include <fstream>
#include <gtest/gtest.h>

TEST(FileTracerTest, OpenSyscall) {
    TempFile test_file{"r4r-test", ""};
    std::ofstream(*test_file) << "test content";

    FileTracer tracer{};

    SyscallMonitor monitor(
        [&test_file]() {
            // we need to use syscall instead of open which uses openat instead
            auto fd = syscall(SYS_open, test_file->c_str(), O_RDONLY,
                              0); // NOLINT(*-pro-type-vararg)
            if (fd == -1) {
                return 1;
            }
            close(fd);
            return 0;
        },
        tracer);

    auto result = monitor.start();
    ASSERT_EQ(result.kind, SyscallMonitor::Result::Exit);
    ASSERT_EQ(result.detail.value(), 0);

    auto const& files = tracer.files();
    ASSERT_EQ(files.size(), 1);
    ASSERT_TRUE(files.contains(*test_file));
    EXPECT_TRUE(files.at(*test_file).existed_before);
    EXPECT_TRUE(files.at(*test_file).size.has_value());
}

TEST(FileTracerTest, OpenAtSyscall) {
    TempFile test_file{"r4r-test-openat", ""};
    std::ofstream(*test_file) << "test content";

    FileTracer tracer{};

    SyscallMonitor monitor(
        [&test_file]() {
            int fd = openat(AT_FDCWD, test_file->c_str(),
                            O_RDONLY); // NOLINT(*-pro-type-vararg)
            if (fd == -1) {
                return 1;
            }
            close(fd);
            return 0;
        },
        tracer);

    auto result = monitor.start();
    ASSERT_EQ(result.kind, SyscallMonitor::Result::Exit);
    ASSERT_EQ(result.detail.value(), 0);

    auto const& files = tracer.files();
    ASSERT_EQ(files.size(), 1);
    ASSERT_TRUE(files.contains(*test_file));
    EXPECT_TRUE(files.at(*test_file).existed_before);
    EXPECT_TRUE(files.at(*test_file).size.has_value());
}

TEST(FileTracerTest, ExecveSyscall) {
    char const* executable = "/bin/true";

    FileTracer tracer{};

    SyscallMonitor monitor(
        [&executable]() {
            pid_t pid = fork();
            if (pid == 0) {
                execl(executable, executable,
                      nullptr); // NOLINT(*-pro-type-vararg)
                _exit(127);
            }
            int status{};
            waitpid(pid, &status, 0);
            return WEXITSTATUS(status);
        },
        tracer);

    auto result = monitor.start();
    ASSERT_EQ(result.kind, SyscallMonitor::Result::Exit);
    ASSERT_EQ(result.detail.value(), 0);

    auto const& files = tracer.files();
    ASSERT_GT(files.size(), 0);
    ASSERT_TRUE(files.contains(executable));
    EXPECT_TRUE(files.at(executable).existed_before);
    EXPECT_TRUE(files.at(executable).size.has_value());
}

TEST(FileTracerTest, IgnoreFilesExtended) {
    TempFile test_file1{"r4r-test-ignore1", ""};
    std::ofstream(*test_file1) << "test content 1";

    TempFile test_file2{"r4r-test-ignore2", ""};
    std::ofstream(*test_file2) << "test content 2";

    FileSystemTrie<bool> ignore_files;
    ignore_files.insert(test_file1->c_str(), true);

    FileTracer tracer{ignore_files};

    SyscallMonitor monitor(
        [&test_file1, &test_file2]() {
            int fd1 = open(test_file1->c_str(),
                           O_RDONLY); // NOLINT(*-pro-type-vararg)
            if (fd1 == -1) {
                return 1;
            }
            close(fd1);

            int fd2 = open(test_file2->c_str(),
                           O_RDONLY); // NOLINT(*-pro-type-vararg)
            if (fd2 == -1) {
                return 1;
            }
            close(fd2);

            return 0;
        },
        tracer);

    auto result = monitor.start();
    ASSERT_EQ(result.kind, SyscallMonitor::Result::Exit);
    ASSERT_EQ(result.detail.value(), 0);

    auto const& files = tracer.files();
    ASSERT_EQ(files.size(), 1);
    ASSERT_TRUE(files.contains(*test_file2));
    EXPECT_TRUE(files.at(*test_file2).existed_before);
    EXPECT_TRUE(files.at(*test_file2).size.has_value());
}