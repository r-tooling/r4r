#include "util_fs.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace fs = std::filesystem;

class SymlinkResolverTest : public ::testing::Test {
  public:
    fs::path temp_root;
    // <temp>/target
    fs::path target_dir;
    // <temp>/symlink1 -> <temp>/target
    fs::path symlink1_dir;
    // <temp>/target/test
    fs::path test_file;
    // <temp>/symlink2 -> <temp>/target/test
    fs::path symlink2_file;
    // <temp>/target/symlink3 -> <temp>/target/test
    fs::path symlink3_file;
    // <temp>/symlink4 -> <temp>/symlink5
    fs::path symlink4_file;
    // <temp>/symlink5 -> <temp>/target/test
    fs::path symlink5_file;

    SymlinkResolver resolver;

  protected:
    // <temp>
    // ├── symlink1 -> <temp>/target
    // ├── symlink2 -> <temp>/target/test
    // ├── symlink4 -> <temp>/symlink5
    // ├── symlink5 -> <temp>/target/test
    // └── target
    //     ├── symlink3 -> <temp>/target/test
    //     └── test

    void SetUp() override {
        temp_root = TempFile::create_temp_file("SymlinkResolverTest", "");
        fs::create_directory(temp_root);

        target_dir = temp_root / "target";
        fs::create_directory(target_dir);

        symlink1_dir = temp_root / "symlink1";
        fs::create_symlink(target_dir, symlink1_dir);

        test_file = target_dir / "test";
        { std::ofstream f(test_file); }

        symlink2_file = temp_root / "symlink2";
        fs::create_symlink(test_file, symlink2_file);

        symlink3_file = target_dir / "symlink3";
        fs::create_symlink(test_file, symlink3_file);

        symlink5_file = temp_root / "symlink5";
        fs::create_symlink(test_file, symlink5_file);

        symlink4_file = temp_root / "symlink4";
        fs::create_symlink(symlink5_file, symlink4_file);

        resolver = SymlinkResolver(temp_root);
    }

    void TearDown() override { fs::remove_all(temp_root); }
};

TEST_F(SymlinkResolverTest, ReturnsOriginalPathWhenNoMappingApplies) {
    fs::path outside_file = temp_root / "outside.txt";
    { std::ofstream f(outside_file); }
    auto results = resolver.resolve_symlinks(outside_file);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results.contains(outside_file));
}

// <temp>/target/test -> { <temp>/target/test, <temp>/symlink1/test }
TEST_F(SymlinkResolverTest, ResolvesTargetToSymlinkPath) {
    fs::path symlink_file = symlink1_dir / test_file.filename();

    auto results = resolver.resolve_symlinks(test_file);

    ASSERT_EQ(results.size(), 2u);
    EXPECT_TRUE(results.contains(test_file));
    EXPECT_TRUE(results.contains(symlink_file));
}

// <temp>/symlink1/test -> { <temp>/symlink1/test, <temp>/target/test }
TEST_F(SymlinkResolverTest, ResolvesSymlinkPathToTargetPath) {
    fs::path symlink_file = symlink1_dir / test_file.filename();
    ASSERT_TRUE(fs::exists(symlink_file));

    auto results = resolver.resolve_symlinks(symlink_file);

    ASSERT_EQ(results.size(), 2u);
    EXPECT_TRUE(results.contains(symlink_file));
    EXPECT_TRUE(results.contains(test_file));
}

TEST_F(SymlinkResolverTest, DoesNotAddNonexistentCandidate) {
    fs::remove(test_file);
    auto results = resolver.resolve_symlinks(test_file);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results.contains(test_file));
}

TEST_F(SymlinkResolverTest, NoSymlinksReturnOriginalFile) {
    SymlinkResolver resolver;
    auto results = resolver.resolve_symlinks(test_file);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results.contains(test_file));
}

// <temp>/symlink1/symlink3 -> {
//      <temp>/symlink1/symlink3,
//      <temp>/target/test,
//      <temp>/symlink1/test,
//      <temp>/target/symlink3,
//  }
TEST_F(SymlinkResolverTest, TestSymlinks) {
    auto results = resolver.resolve_symlinks(symlink3_file);

    ASSERT_EQ(results.size(), 4u);
    EXPECT_TRUE(results.contains(symlink3_file));
    EXPECT_TRUE(results.contains(test_file));
    EXPECT_TRUE(results.contains(symlink1_dir / test_file.filename()));
    EXPECT_TRUE(results.contains(target_dir / symlink3_file.filename()));
}

//  <temp>/symlink2 -> {
//     <temp>/symlink2,
//     <temp>/target/test,
//     <temp>/symlink1/test
//  }
TEST_F(SymlinkResolverTest, TestSymlinksViaSymlinks) {
    auto results = resolver.resolve_symlinks(symlink2_file);

    ASSERT_EQ(results.size(), 3u);
    EXPECT_TRUE(results.contains(symlink2_file));
    EXPECT_TRUE(results.contains(test_file));
    EXPECT_TRUE(results.contains(symlink1_dir / test_file.filename()));
}

//  <temp>/symlink4 -> {
//     <temp>/symlink4,
//     <temp>/symlink5,
//     <temp>/target/test,
//     <temp>/symlink1/test
//  }
TEST_F(SymlinkResolverTest, RecursiveSymlinks) {
    auto results = resolver.resolve_symlinks(symlink4_file);

    ASSERT_EQ(results.size(), 4u);
    EXPECT_TRUE(results.contains(symlink5_file));
    EXPECT_TRUE(results.contains(symlink4_file));
    EXPECT_TRUE(results.contains(test_file));
    EXPECT_TRUE(results.contains(symlink1_dir / test_file.filename()));
}
