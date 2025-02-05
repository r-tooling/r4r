#include "argparser.h"
#include <gtest/gtest.h>
#include <vector>

TEST(ArgumentParserTest, BasicOptionParsing) {
    ArgumentParser parser("Test", "test");
    bool verbose_flag = false;

    parser.add_option('v', "verbose").with_callback([&](std::string const&) {
        verbose_flag = true;
    });

    char const* argv[] = {"test", "-v"};
    auto result = parser.parse(2, const_cast<char**>(argv));

    EXPECT_TRUE(verbose_flag);
    EXPECT_TRUE(result.contains("v"));
    EXPECT_TRUE(result.contains("verbose"));
}

TEST(ArgumentParserTest, PositionalArgumentParsing) {
    ArgumentParser parser("Test", "test");
    std::string input_file;

    parser.add_positional("input")
        .with_callback([&](std::string const& val) { input_file = val; })
        .required();

    char const* argv[] = {"test", "data.txt"};
    auto result = parser.parse(2, const_cast<char**>(argv));

    EXPECT_EQ(input_file, "data.txt");
    EXPECT_EQ(result.get_positional("input").size(), 1);
    EXPECT_EQ(result.get_positional("input")[0], "data.txt");
}

TEST(ArgumentParserTest, OptionWithArgument) {
    ArgumentParser parser("Test", "test");
    std::string log_level;

    parser.add_option('l', "level")
        .has_argument()
        .with_callback([&](std::string const& val) { log_level = val; });

    char const* argv[] = {"test", "--level=3"};
    auto result = parser.parse(2, const_cast<char**>(argv));

    EXPECT_EQ(log_level, "3");
    EXPECT_EQ(result.get("level"), "3");
}

TEST(ArgumentParserTest, DefaultValues) {
    ArgumentParser parser("Test", "test");
    parser.add_option('d', "debug").with_default("1");

    char const* argv[] = {"test"};
    auto result = parser.parse(1, const_cast<char**>(argv));

    EXPECT_EQ(result.get("debug"), "1");
}

TEST(ArgumentParserTest, RequiredOptionEnforcement) {
    ArgumentParser parser("Test", "test");
    parser.add_option('r', "required").required();

    char const* argv[] = {"test"};
    EXPECT_THROW(parser.parse(1, const_cast<char**>(argv)), std::runtime_error);
}

TEST(ArgumentParserTest, MixedArguments) {
    ArgumentParser parser("Test", "test");
    std::string input_file, output_file;
    int verbosity = 0;

    parser.add_option('v', "verbose").with_callback([&](std::string const&) {
        verbosity++;
    });

    parser.add_option('i', "input")
        .has_argument()
        .with_callback([&](std::string const& val) { input_file = val; });

    parser.add_positional("output").with_callback(
        [&](std::string const& val) { output_file = val; });

    char const* argv[] = {"test", "-vi", "in.txt", "out.txt"};
    auto result = parser.parse(4, const_cast<char**>(argv));

    EXPECT_EQ(verbosity, 1);
    EXPECT_EQ(input_file, "in.txt");
    EXPECT_EQ(result.get("i"), "in.txt");
    EXPECT_EQ(output_file, "out.txt");
    EXPECT_EQ(result.get_positional("output").size(), 1);
}

TEST(ArgumentParserTest, GroupedShortOptions) {
    ArgumentParser parser("Test", "test");
    bool a_flag = false, b_flag = false;

    parser.add_option('a').with_callback(
        [&](std::string const&) { a_flag = true; });
    parser.add_option('b').with_callback(
        [&](std::string const&) { b_flag = true; });

    char const* argv[] = {"test", "-ab"};
    auto result = parser.parse(2, const_cast<char**>(argv));

    EXPECT_TRUE(a_flag);
    EXPECT_TRUE(b_flag);
    EXPECT_TRUE(result.contains("a"));
    EXPECT_TRUE(result.contains("b"));
}

TEST(ArgumentParserTest, ErrorOnUnknownOption) {
    ArgumentParser parser("Test", "test");
    char const* argv[] = {"test", "--unknown"};
    EXPECT_THROW(parser.parse(2, const_cast<char**>(argv)),
                 ArgumentParserException);
}

TEST(ArgumentParserTest, MultiplePositionals) {
    ArgumentParser parser("Test", "test");
    std::vector<std::string> files;

    parser.add_positional("files").required().multiple().with_callback(
        [&](std::string const& val) { files.push_back(val); });

    char const* argv[] = {"test", "a.txt", "b.txt", "c.txt"};
    auto result = parser.parse(4, const_cast<char**>(argv));

    ASSERT_EQ(files.size(), 3);
    EXPECT_EQ(result.get_positional("files").size(), 3);
    EXPECT_EQ(result.get_positional("files")[0], "a.txt");
    EXPECT_EQ(result.get_positional("files")[1], "b.txt");
    EXPECT_EQ(result.get_positional("files")[2], "c.txt");
}

TEST(ArgumentParserTest, HelpOutput) {
    ArgumentParser parser("sample", "Sample Program");
    parser.add_option('v', "verbose").with_help("Enable verbose output");
    parser.add_option('o', "output").has_argument().with_metavar("FILE");
    parser.add_option("help").with_help("Prints this message");
    parser.add_positional("input").required().with_help("Input file");
    parser.add_positional("very long positional name").required().with_help("Help text");

    std::string const expected = R"(Sample Program

Usage: sample [OPTIONS] <input> <very long positional name>

Options:
  -v, --verbose        Enable verbose output
  -o, --output FILE
  --help               Prints this message

Positional arguments:
  input                        Input file
  very long positional name    Help text
)";

    std::string const help = parser.help();
    std::cout << help;
    EXPECT_EQ(help, expected);
}

TEST(ArgumentParserTest, ArgumentWithEqualSign) {
    ArgumentParser parser("Test", "test");
    std::string config_path;

    parser.add_option('c', "config")
        .has_argument()
        .with_callback([&](std::string const& val) { config_path = val; });

    char const* argv[] = {"test", "--config=settings.conf"};
    auto result = parser.parse(2, const_cast<char**>(argv));

    EXPECT_EQ(config_path, "settings.conf");
    EXPECT_EQ(result.get("config"), "settings.conf");
}

TEST(ArgumentParserTest, MissingRequiredPositional) {
    ArgumentParser parser("Test", "test");
    parser.add_positional("input").required();

    char const* argv[] = {"test"};
    EXPECT_THROW(parser.parse(1, const_cast<char**>(argv)), std::runtime_error);
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
