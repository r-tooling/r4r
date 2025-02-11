#include "process.h"
#include <gtest/gtest.h>

TEST(CommandTest, TestOutput) {
    auto out = Command("echo").arg("Hello").arg("World").output();

    EXPECT_EQ(out.stdout_data, "Hello World\n");
    EXPECT_EQ(0, out.exit_code);
}

TEST(CommandTest, TestEnvAndWorkingDir) {
    auto out = Command("pwd").current_dir("/tmp").output();

    EXPECT_EQ(out.stdout_data, "/tmp\n");
    EXPECT_EQ(0, out.exit_code);
}

TEST(CommandTest, TestMergeStderrToStdout) {
    auto out = Command("sh")
                   .arg("-c")
                   .arg("echo STDOUT && echo STDERR 1>&2")
                   .set_stderr(Stdio::Merge)
                   .output();

    EXPECT_NE(std::string::npos, out.stdout_data.find("STDOUT"));
    EXPECT_NE(std::string::npos, out.stdout_data.find("STDERR"));
    EXPECT_TRUE(out.stderr_data.empty());
}

TEST(CommandTest, TestNonExistingCommand) {
    auto out = Command("this_command_does_not_exist").output();

    EXPECT_EQ(127, out.exit_code);
}

TEST(CommandTest, TestNonZeroExitCode) {
    auto out = Command("sh").arg("-c").arg("exit 42").output();

    EXPECT_EQ(42, out.exit_code);
}

TEST(CommandTest, TestKillProcess) {
    auto child = Command("sleep").arg("9999").spawn();

    auto maybe_code = child.try_wait();
    EXPECT_FALSE(maybe_code.has_value());

    child.kill();

    int exit_code = child.wait();
    EXPECT_NE(0, exit_code);
}

TEST(ProcessCwdTest, SpawnProcessInTmp) {
    pid_t pid = fork();
    if (pid < 0) {
        throw std::runtime_error("fork() failed: " +
                                 std::string(strerror(errno)));
    }
    if (pid == 0) {
        // child
        chdir("/tmp");
        pause();
        _exit(0);
    } else {
        // parent
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        auto child_cwd = get_process_cwd(pid);
        EXPECT_TRUE(child_cwd.has_value());
        EXPECT_EQ(*child_cwd, "/tmp");

        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
    }
}

TEST(ResolveFdFilenameTest, Valid) {
    char temp_filename[] = "/tmp/testfileXXXXXX";
    int temp_fd = mkstemp(temp_filename);
    ASSERT_NE(temp_fd, -1) << "Failed to create a temporary file.";

    write(temp_fd, "test", 4);

    auto resolved_path = resolve_fd_filename(getpid(), temp_fd);

    ASSERT_TRUE(resolved_path.has_value());
    EXPECT_EQ(resolved_path.value(), temp_filename);

    close(temp_fd);
    unlink(temp_filename);
}

TEST(ResolveFdFilenameTest, Invalid) {
    int invalid_fd = -1;

    auto resolved_path = resolve_fd_filename(getpid(), invalid_fd);

    EXPECT_FALSE(resolved_path.has_value());
}
