#include "util_fs.h"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#define SKIP_ON_CI(message)                                                    \
    if (std::getenv("GITHUB_ACTIONS") != nullptr) {                            \
        GTEST_SKIP() << message;                                               \
    }

#define SKIP_ON_ROOT(message)                                                  \
    if (geteuid() == 0) {                                                      \
        GTEST_SKIP() << message;                                               \
    }

class CheckAccessibilityTest : public ::testing::Test {
  public:
    fs::path temp_dir;

  protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "check_accessibility_test";
        fs::remove_all(temp_dir);
        fs::create_directories(temp_dir);
    }

    void TearDown() override { fs::remove_all(temp_dir); }
};

TEST_F(CheckAccessibilityTest, non_existent_file) {
    fs::path nonexistent_file = temp_dir / "nonexistent.txt";
    auto status = check_accessibility(nonexistent_file);
    EXPECT_EQ(status, AccessStatus::DoesNotExist);
}

TEST_F(CheckAccessibilityTest, existing_file_readable) {
    fs::path readable_file = temp_dir / "readable.txt";
    { std::ofstream ofs(readable_file); }
    auto status = check_accessibility(readable_file);
    EXPECT_EQ(status, AccessStatus::Accessible);
}

TEST_F(CheckAccessibilityTest, directory_readable) {
    fs::path readable_dir = temp_dir / "readable_dir";
    fs::create_directories(readable_dir);

    auto status = check_accessibility(readable_dir);
    EXPECT_EQ(status, AccessStatus::Accessible);
}

TEST_F(CheckAccessibilityTest, file_insufficient_permission) {
    SKIP_ON_ROOT("root has access to everything!");

    fs::path unreadable_file = temp_dir / "unreadable.txt";
    { std::ofstream ofs(unreadable_file); }

    fs::permissions(unreadable_file, fs::perms::all, fs::perm_options::remove);

    auto status = check_accessibility(unreadable_file);
    EXPECT_EQ(status, AccessStatus::InsufficientPermission);

    fs::permissions(unreadable_file, fs::perms::owner_read,
                    fs::perm_options::add);
}

TEST_F(CheckAccessibilityTest, directory_insufficient_permission) {
    SKIP_ON_ROOT("root has access to everything!");

    fs::path unreadable_dir = temp_dir / "unreadable_dir";
    fs::create_directories(unreadable_dir);

    fs::permissions(unreadable_dir, fs::perms::all, fs::perm_options::remove);

    auto status = check_accessibility(unreadable_dir);
    EXPECT_EQ(status, AccessStatus::InsufficientPermission);

    fs::permissions(unreadable_dir, fs::perms::owner_read,
                    fs::perm_options::add);
}
