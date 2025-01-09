#include "../util.hpp"
#include <fstream>
#include <gtest/gtest.h>

// Demonstrate some basic assertions.
TEST(UtilTest, FilesystemTrie) {
    util::FilesystemTrie<bool> trie{false};

    trie.insert("/dev", true);
    trie.insert("/dev/null", false);

    //    auto p = [](bool value) { return value; };

    EXPECT_TRUE(*trie.find("/dev"));
    EXPECT_FALSE(*trie.find("/dev/null"));
    EXPECT_EQ(trie.find("/dev/tty"), nullptr);

    EXPECT_TRUE(trie.find_last_matching("/dev"));
    EXPECT_FALSE(trie.find_last_matching("/dev/null"));
    EXPECT_TRUE(trie.find_last_matching("/dev/tty"));
}

TEST(UtilTest, CreateTarArchiveTest) {
    auto temp_dir = fs::temp_directory_path() / "tar_archive_test";
    if (fs::exists(temp_dir)) {
        fs::remove_all(temp_dir);
    }
    fs::create_directory(temp_dir);

    std::array<fs::path, 5> files;
    std::string files_str;
    for (size_t i = 1; i <= files.size(); i++) {
        auto f = temp_dir / ("file" + std::to_string(i) + ".txt");
        std::ofstream fs(f);
        fs << "file" << i << ".";
        fs.close();
        files_str += f.string() + "\n";
        files[i - 1] = f;
    }

    auto archive = temp_dir / "archive.tar";

    util::create_tar_archive(archive, files);

    EXPECT_TRUE(fs::exists(archive));

    auto out = util::execute_command("tar tf " + archive.string() +
                                     " --absolute-names");

    EXPECT_EQ(out, files_str);

    fs::remove_all(temp_dir);
}

TEST(UtilTest, EscapeEnvVarDefinition) {
    using namespace util;
    // clang-format off
    EXPECT_EQ(escape_env_var_definition("key=value"), "key=\"value\"");
    EXPECT_EQ(escape_env_var_definition("key=\"value\""), "key=\"value\"");
    EXPECT_EQ(escape_env_var_definition("key= value"), "key=\" value\"");
    EXPECT_EQ(escape_env_var_definition("key="), "key=\"\"");
    EXPECT_EQ(escape_env_var_definition("=value"), "=\"value\"");
    EXPECT_EQ(escape_env_var_definition("key=value "), "key=\"value \"");
    EXPECT_EQ(escape_env_var_definition("key=  value "), "key=\"  value \"");
    EXPECT_EQ(escape_env_var_definition("key"), "key");
    EXPECT_EQ(escape_env_var_definition("= "), "=\" \"");
    EXPECT_EQ(escape_env_var_definition("key=\tvalue"), "key=\"\tvalue\"");
    EXPECT_EQ(escape_env_var_definition("key=\n"), "key=\"\n\"");
    EXPECT_EQ(escape_env_var_definition("key=\r"), "key=\"\r\"");
    EXPECT_EQ(escape_env_var_definition("key=\f"), "key=\"\f\"");
    EXPECT_EQ(escape_env_var_definition("key=\v"), "key=\"\v\"");
    EXPECT_EQ(escape_env_var_definition("key= \t"), "key=\" \t\"");
    // clang-format on
}

TEST(UtilTest, EscapeCmdArg) {
    using namespace util;
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
