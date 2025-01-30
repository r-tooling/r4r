#include "util.h"
#include <fstream>
#include <gtest/gtest.h>

// TEST(UtilTest, CreateTarArchiveTest) {
//     auto temp_dir = fs::temp_directory_path() / "tar_archive_test";
//     if (fs::exists(temp_dir)) {
//         fs::remove_all(temp_dir);
//     }
//     fs::create_directory(temp_dir);
//
//     std::array<fs::path, 5> files;
//     std::string files_str;
//     for (size_t i = 1; i <= files.size(); i++) {
//         auto f = temp_dir / ("file" + std::to_string(i) + ".txt");
//         std::ofstream fs(f);
//         fs << "file" << i << ".";
//         fs.close();
//         files_str += f.string() + "\n";
//         files[i - 1] = f;
//     }
//
//     auto archive = temp_dir / "archive.tar";
//
//     create_tar_archive(archive, files);
//
//     EXPECT_TRUE(fs::exists(archive));
//
//     auto [out, exit_code] =
//         execute_command({"tar", "tf", archive.string(), "--absolute-names"});
//
//     EXPECT_EQ(exit_code, 0);
//     EXPECT_EQ(out, files_str);
//
//     fs::remove_all(temp_dir);
// }

TEST(UtilTest, EscapeCmdArg) {
    // clang-format off
    EXPECT_EQ(escape_cmd_arg("no special chars"), "'no special chars'");
    EXPECT_EQ(escape_cmd_arg("arg with spaces"), "'arg with spaces'");
    EXPECT_EQ(escape_cmd_arg("arg with \"quotes\""), "'arg with \"quotes\"'");
    EXPECT_EQ(escape_cmd_arg("arg with $dollar"), "'arg with $dollar'");
    EXPECT_EQ(escape_cmd_arg("arg with back\\slash"), "'arg with back\\slash'");
    EXPECT_EQ(escape_cmd_arg("arg with `backtick`"), "'arg with `backtick`'");
    EXPECT_EQ(escape_cmd_arg("arg with ;semicolon"), "'arg with ;semicolon'");
    EXPECT_EQ(escape_cmd_arg("arg with &ampersand"), "'arg with &ampersand'");
    EXPECT_EQ(escape_cmd_arg("arg with |pipe"), "'arg with |pipe'");
    EXPECT_EQ(escape_cmd_arg("arg with *asterisk"), "'arg with *asterisk'");
    EXPECT_EQ(escape_cmd_arg("arg with ?question"), "'arg with ?question'");
    EXPECT_EQ(escape_cmd_arg("arg with [brackets]"), "'arg with [brackets]'");
    EXPECT_EQ(escape_cmd_arg("arg with (parenthesis)"), "'arg with (parenthesis)'");
    EXPECT_EQ(escape_cmd_arg("arg with <less"), "'arg with <less'");
    EXPECT_EQ(escape_cmd_arg("arg with >greater"), "'arg with >greater'");
    EXPECT_EQ(escape_cmd_arg("arg with #hash"), "'arg with #hash'");
    EXPECT_EQ(escape_cmd_arg("arg with !exclamation"), "'arg with !exclamation'");
    EXPECT_EQ(escape_cmd_arg("arg with 'single quote'"), "'arg with '\\''single quote'\\'''");
    EXPECT_EQ(escape_cmd_arg(""), "''");
    EXPECT_EQ(escape_cmd_arg("'already quoted'"), "''\\''already quoted'\\'''");
    // clang-format on
}

TEST(CollectionToCArrayTest, VectorTest) {
    std::vector<int> col = {1, 2, 3};
    auto c_arr = collection_to_c_array(col);

    ASSERT_NE(c_arr, nullptr);
    ASSERT_EQ(c_arr[0], 1);
    ASSERT_EQ(c_arr[1], 2);
    ASSERT_EQ(c_arr[2], 3);
    ASSERT_EQ(c_arr[3], 0); // Null terminator
}

TEST(CollectionToCArrayTest, ArrayTest) {
    std::array<double, 2> col = {1.5, 2.7};
    auto c_arr = collection_to_c_array(col);

    ASSERT_NE(c_arr, nullptr);
    ASSERT_DOUBLE_EQ(c_arr[0], 1.5);
    ASSERT_DOUBLE_EQ(c_arr[1], 2.7);
    ASSERT_DOUBLE_EQ(c_arr[2], 0.0);
}

TEST(CollectionToCArrayTest, EmptyContainerTest) {
    std::vector<int> col;
    auto c_arr = collection_to_c_array(col);
    ASSERT_EQ(c_arr, nullptr);
}

TEST(CollectionToCArrayTest, StringTest) {
    std::vector<std::string> col = {"one", "two", "three"};
    auto c_arr = collection_to_c_array(col);

    ASSERT_NE(c_arr, nullptr);
    for (int i = 0; i < col.size(); i++) {
        ASSERT_EQ(c_arr[i], col[i].c_str());
    }
    ASSERT_EQ(c_arr[3], nullptr);
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

using namespace std::chrono_literals;

TEST(FormatElapsedTime, Milliseconds) {
    using namespace std::chrono;
    EXPECT_EQ(format_elapsed_time(999ms), "999ms");
}

TEST(FormatElapsedTime, SecondsWithPrecision) {
    using namespace std::chrono;
    EXPECT_EQ(format_elapsed_time(1234ms), "1.2s");
    EXPECT_EQ(format_elapsed_time(59999ms), "60.0s");
}

TEST(FormatElapsedTime, MinutesAndSeconds) {
    using namespace std::chrono;
    EXPECT_EQ(format_elapsed_time(1min), "1:00.0");
    EXPECT_EQ(format_elapsed_time(12min + 34s + 321ms), "12:34.3");
}

TEST(FormatElapsedTime, HoursMinutesSeconds) {
    using namespace std::chrono;
    EXPECT_EQ(format_elapsed_time(1h), "1:00:00");
    EXPECT_EQ(format_elapsed_time(1h + 1min + 1s), "1:01:01");
}

TEST(StringSplitNTest, BasicSplit) {
    auto result = string_split_n<3>("apple,banana,cherry", ",");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at(0), "apple");
    EXPECT_EQ(result->at(1), "banana");
    EXPECT_EQ(result->at(2), "cherry");
}

TEST(StringSplitNTest, EmptyString) {
    auto result = string_split_n<1>("", ",");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at(0), "");
}

TEST(StringSplitNTest, SingleElement) {
    auto result = string_split_n<1>("apple", ",");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at(0), "apple");
}

TEST(StringSplitNTest, TooManySplits) {
    auto result = string_split_n<2>("apple,banana,cherry", ",");
    ASSERT_FALSE(result.has_value());
}

TEST(StringSplitNTest, NotEnoughSplits) {
    auto result = string_split_n<3>("apple,banana", ",");
    ASSERT_FALSE(result.has_value());
}

TEST(StringSplitNTest, EmptyDelimiter) {
    auto result = string_split_n<3>("apple,banana,cherry", "");
    ASSERT_FALSE(result.has_value());
}

TEST(StringSplitNTest, DelimiterAtBeginning) {
    auto result = string_split_n<2>(",apple", ",");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at(0), "");
    EXPECT_EQ(result->at(1), "apple");
}

TEST(StringSplitNTest, DelimiterAtEnd) {
    auto result = string_split_n<3>("apple,banana,", ",");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at(0), "apple");
    EXPECT_EQ(result->at(1), "banana");
    EXPECT_EQ(result->at(2), "");
}

TEST(StringSplitNTest, ConsecutiveDelimiters) {
    auto result = string_split_n<3>("apple,,cherry", ",");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at(0), "apple");
    EXPECT_EQ(result->at(1), "");
    EXPECT_EQ(result->at(2), "cherry");
}

TEST(StringSplitNTest, NonBreakingSpaceDelimiter) {
    std::string nonBreakingSpace = "\u00A0"; // UTF-8 for non-breaking space
    auto result = string_split_n<2>("apple" + nonBreakingSpace + "banana",
                                    nonBreakingSpace);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at(0), "apple");
    EXPECT_EQ(result->at(1), "banana");
}
