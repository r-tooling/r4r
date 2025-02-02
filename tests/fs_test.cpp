#include "fs.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <unordered_map>

namespace fs = std::filesystem;

class SymlinkResolverTest : public ::testing::Test {
  protected:
    fs::path temp_root;

    fs::path target_dir;
    fs::path symlink_dir;
    fs::path test_file;

    // Our resolver, which we construct with our own mapping.
    std::unique_ptr<SymlinkResolver> resolver;

    void SetUp() override {
        temp_root = TempFile::create_temp_file("SymlinkResolverTest", "");
        fs::create_directory(temp_root);

        target_dir = temp_root / "target";
        fs::create_directory(target_dir);

        test_file = target_dir / "test.txt";
        { std::ofstream f(test_file); }

        symlink_dir = temp_root / "link";
        fs::create_symlink(target_dir, symlink_dir);

        std::unordered_map<fs::path, fs::path> mapping;
        mapping[symlink_dir] = target_dir;

        resolver = std::make_unique<SymlinkResolver>(mapping);
    }

    void TearDown() override { fs::remove_all(temp_root); }
};

TEST_F(SymlinkResolverTest, ReturnsOriginalPathWhenNoMappingApplies) {
    fs::path outside_file = temp_root / "outside.txt";
    { std::ofstream f(outside_file); }
    auto results = resolver->get_root_symlink(outside_file);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], outside_file);
}

TEST_F(SymlinkResolverTest, ResolvesTargetToSymlinkPath) {
    auto results = resolver->get_root_symlink(test_file);

    fs::path candidate = symlink_dir / test_file.lexically_relative(target_dir);

    EXPECT_NE(std::find(results.begin(), results.end(), test_file),
              results.end());
    EXPECT_NE(std::find(results.begin(), results.end(), candidate),
              results.end());
}

TEST_F(SymlinkResolverTest, ResolvesSymlinkPathToTargetPath) {
    fs::path symlink_file = symlink_dir / "test.txt";
    ASSERT_TRUE(fs::exists(symlink_file));

    auto results = resolver->get_root_symlink(symlink_file);

    fs::path candidate =
        target_dir / symlink_file.lexically_relative(symlink_dir);

    EXPECT_NE(std::find(results.begin(), results.end(), symlink_file),
              results.end());
    EXPECT_NE(std::find(results.begin(), results.end(), candidate),
              results.end());
}

TEST_F(SymlinkResolverTest, DoesNotAddNonexistentCandidate) {
    fs::remove(test_file);
    auto results = resolver->get_root_symlink(test_file);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], test_file);
}

TEST_F(SymlinkResolverTest, NoSymlinksReturnOriginalFile) {
    SymlinkResolver resolver{};
    auto results = resolver.get_root_symlink(test_file);

    // Expect only the original path since there are no symlink mappings.
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], test_file);
}
