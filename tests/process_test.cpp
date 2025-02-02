#include "process.h"
#include <gtest/gtest.h>
#include <sstream>
#include <variant>

// Test launching a process and reading output directly
TEST(ProcessClassTest, ReadOutput) {
    Process proc({"echo", "Hello"});
    std::string line;
    std::getline(proc.output(), line);
    EXPECT_EQ(line, "Hello");
}

// Test getting process exit code
TEST(ProcessClassTest, ExitCodeCheck) {
    Process proc({"sh", "-c", "exit 42"});
    EXPECT_EQ(proc.wait(), 42);
}

// Test merging stderr into stdout
TEST(ProcessClassTest, StderrMerge) {
    Process proc({"sh", "-c", "echo out; echo err >&2"}, true);
    std::string output, line;
    while (std::getline(proc.output(), line)) {
        output += line + "\n";
    }

    EXPECT_EQ(proc.wait(), 0);
    EXPECT_NE(output.find("out"), std::string::npos);
    EXPECT_NE(output.find("err"), std::string::npos);
}

// Test invalid command execution
TEST(ProcessClassTest, InvalidCommand) {
    Process proc({"invalid_command_which_does_not_exist"});
    std::string output;
    std::getline(proc.output(), output);
    EXPECT_TRUE(output.empty());
    while (proc.is_running())
        ;
    EXPECT_NE(proc.exit_code(), 0);
}

// Test execute_command returning a string
TEST(ExecuteCommandTest, OutputAsString) {
    auto [output, exit_code] = execute_command({"echo", "Hello"});
    EXPECT_EQ(exit_code, 0);
    EXPECT_EQ(output, "Hello\n");
}

// Test execute_command merging stderr
TEST(ExecuteCommandTest, OutputWithStderr) {
    auto [output, exit_code] =
        execute_command({"sh", "-c", "echo out; echo err >&2"}, true);
    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("out"), std::string::npos);
    EXPECT_NE(output.find("err"), std::string::npos);
}

TEST(ExecuteCommandTest, OutputWithNonZeroOutput) {
    auto [output, exit_code] =
        execute_command({"sh", "-c", "echo out; exit 42"});
    EXPECT_EQ(exit_code, 42);
    EXPECT_NE(output.find("out"), std::string::npos);
}

// Test execute_command with lambda callback
TEST(ExecuteCommandTest, LambdaCallback) {
    std::vector<std::string> lines;
    int exit_code = -1;

    execute_command({"sh", "-c", "echo Line1; echo Line2; exit 7"},
                    [&](std::variant<std::string, int> v) {
                        if (std::holds_alternative<std::string>(v)) {
                            lines.push_back(std::get<std::string>(v));
                        } else {
                            exit_code = std::get<int>(v);
                        }
                    });

    EXPECT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0], "Line1");
    EXPECT_EQ(lines[1], "Line2");
    EXPECT_EQ(exit_code, 7);
}

// Test execute_command handling an invalid command
TEST(ExecuteCommandTest, InvalidCommand) {
    auto [output, exit_code] =
        execute_command({"invalid_command_which_does_not_exist"});
    EXPECT_NE(exit_code, 0);
    EXPECT_TRUE(output.empty());

    exit_code = -1;
    execute_command({"invalid_command_which_does_not_exist"},
                    [&](std::variant<std::string, int> v) {
                        if (std::holds_alternative<int>(v)) {
                            exit_code = std::get<int>(v);
                        }
                    });

    EXPECT_NE(exit_code, 0);
}

// Test wait() method and its return value
TEST(ProcessClassTest, WaitForProcessAndReturnExitCode) {
    Process proc({"sleep", "1"});
    EXPECT_TRUE(proc.is_running());

    int exit_code = proc.wait();

    EXPECT_FALSE(proc.is_running());
    EXPECT_EQ(exit_code, 0);
}

// Test wait() correctly handles processes terminated by a signal
TEST(ProcessClassTest, SignalExitCode) {
    Process proc({"sh", "-c", "kill -15 $$"});
    int exit_code = proc.wait();         // Wait and get exit code
    EXPECT_EQ(exit_code, 128 + SIGTERM); // Expected: 128 + 15 (SIGTERM)
}

// Test calling wait() multiple times does not change result
TEST(ProcessClassTest, WaitMultipleCalls) {
    Process proc({"sh", "-c", "exit 7"});
    int exit_code1 = proc.wait();
    int exit_code2 = proc.wait();
    EXPECT_EQ(exit_code1, 7);
    EXPECT_EQ(exit_code2, 7);
}
