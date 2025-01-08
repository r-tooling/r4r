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
        files[i-1] = f;
    }

    auto archive = temp_dir / "archive.tar";

    util::create_tar_archive(archive, files);

    EXPECT_TRUE(fs::exists(archive));

    auto out = util::execute_command("tar tf " + archive.string() +
                                     " --absolute-names");

    EXPECT_EQ(out, files_str);

    fs::remove_all(temp_dir);
}
