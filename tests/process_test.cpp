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