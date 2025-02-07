#include "util.h"
#include <gtest/gtest.h>

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
    EXPECT_EQ(escape_cmd_arg("arg with 'single quote'"), "'arg with \\'single quote\\''");
    EXPECT_EQ(escape_cmd_arg(""), "''");
    EXPECT_EQ(escape_cmd_arg("'already quoted'"), "'\\'already quoted\\''");

    EXPECT_EQ(escape_cmd_arg("arg with \"quotes\"", false), "\"arg with \\\"quotes\\\"\"");
    // clang-format on
}

TEST(CollectionToCArrayTest, EmptyTest) {
    std::vector<std::string> col;
    auto c_arr = collection_to_c_array(col);

    ASSERT_EQ(c_arr.size(), 0);
}

TEST(CollectionToCArrayTest, NonEmptyTest) {
    std::vector<std::string> col = {"one", "two", "three"};
    auto c_arr = collection_to_c_array(col);

    ASSERT_EQ(c_arr.size(), 4);
    for (int i = 0; i < col.size(); i++) {
        ASSERT_EQ(c_arr[i], col[i].c_str());
    }
    ASSERT_EQ(c_arr[3], nullptr);
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
