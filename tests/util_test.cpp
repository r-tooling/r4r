#include "util.h"
#include <gtest/gtest.h>

TEST(EscapeCmdArgTest, NoEscapeNeeded) {
    EXPECT_EQ(escape_cmd_arg("simple"), "simple");
    EXPECT_EQ(escape_cmd_arg(""), "");
}

TEST(EscapeCmdArgTest, ForceEscape) {
    EXPECT_EQ(escape_cmd_arg("simple", true, true), "'simple'");
    EXPECT_EQ(escape_cmd_arg("", true, true), "''");
}

TEST(EscapeCmdArgTest, SingleQuoteEscape) {
    EXPECT_EQ(escape_cmd_arg("needs escaping"), "'needs escaping'");
    EXPECT_EQ(escape_cmd_arg("contains'quote"), "'contains'\\''quote'");
    EXPECT_EQ(escape_cmd_arg("complex example with 'single quotes'"),
              "'complex example with '\\''single quotes'\\'''");
}

TEST(EscapeCmdArgTest, DoubleQuoteEscape) {
    EXPECT_EQ(escape_cmd_arg("needs escaping", false), "\"needs escaping\"");
    EXPECT_EQ(escape_cmd_arg("contains\"quote", false),
              "\"contains\\\"quote\"");
    EXPECT_EQ(escape_cmd_arg("complex example with \"double quotes\"", false),
              "\"complex example with \\\"double quotes\\\"\"");
    EXPECT_EQ(escape_cmd_arg("special chars $ ` \\ !", false),
              "\"special chars \\$ \\` \\\\ \\!\"");
}

TEST(EscapeCmdArgTest, MixedContent) {
    std::string cmd =
        "--bind ctrl-/:toggle-preview --preview 'if [[ -d {} ]]; then command "
        "ls --group-directories-first --color=always -CF {}; else command "
        "batcat --color=always --line-range :500 {}; fi' --bind "
        "ctrl-/:toggle-preview --preview 'if [[ -d {} ]]; then command ls "
        "--group-directories-first --color=always -CF {}; else command batcat "
        "--color=always --line-range :500 {}; fi'";
    std::string expected =
        "\"--bind ctrl-/:toggle-preview --preview 'if [[ -d {} ]]; then "
        "command ls --group-directories-first --color=always -CF {}; else "
        "command batcat --color=always --line-range :500 {}; fi' --bind "
        "ctrl-/:toggle-preview --preview 'if [[ -d {} ]]; then command ls "
        "--group-directories-first --color=always -CF {}; else command batcat "
        "--color=always --line-range :500 {}; fi'\"";
    EXPECT_EQ(escape_cmd_arg(cmd, false), expected);
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

TEST(LoadOsReleaseTest, EmptyInput) {
    std::istringstream input("");
    auto result = load_os_release_map(input);
    EXPECT_TRUE(result.empty());
}

TEST(LoadOsReleaseTest, OnlyCommentsAndEmptyLines) {
    std::istringstream input("# This is a comment\n\n# Another comment\n");
    auto result = load_os_release_map(input);
    EXPECT_TRUE(result.empty());
}

TEST(LoadOsReleaseTest, SingleKeyValuePair) {
    std::istringstream input("NAME=\"Ubuntu\"\n");
    auto result = load_os_release_map(input);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result["NAME"], "Ubuntu");
}

TEST(LoadOsReleaseTest, MultipleKeyValuePairs) {
    std::istringstream input("NAME=\"Ubuntu\"\nVERSION=\"20.04 LTS\"\n");
    auto result = load_os_release_map(input);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result["NAME"], "Ubuntu");
    EXPECT_EQ(result["VERSION"], "20.04 LTS");
}

TEST(LoadOsReleaseTest, TrimmedKeyValuePairs) {
    std::istringstream input(R"( NAME = " Ubuntu " )");
    auto result = load_os_release_map(input);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result["NAME"], "Ubuntu");
}

TEST(LoadOsReleaseTest, UnquotedValue) {
    std::istringstream input("NAME=Ubuntu\n");
    auto result = load_os_release_map(input);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result["NAME"], "Ubuntu");
}

TEST(LoadOsReleaseTest, MixedContent) {
    std::istringstream input("# Comment\nNAME=\"Ubuntu\"\n\nVERSION=\"20.04 "
                             "LTS\"\n# Another comment\n");
    auto result = load_os_release_map(input);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result["NAME"], "Ubuntu");
    EXPECT_EQ(result["VERSION"], "20.04 LTS");
}

TEST(LoadOsReleaseTest, InvalidFormat) {
    std::istringstream input("INVALID_LINE\nNAME=\"Ubuntu\"\n");
    auto result = load_os_release_map(input);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result["NAME"], "Ubuntu");
}
