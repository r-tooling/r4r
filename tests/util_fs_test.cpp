#include "util_fs.h"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

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
    {
        std::ofstream ofs(readable_file);
    }
    auto status = check_accessibility(readable_file);
    EXPECT_EQ(status, AccessStatus::Accessible);
}

TEST_F(CheckAccessibilityTest, directory_readable) {
    fs::path readable_dir = temp_dir / "readable_dir";
    fs::create_directories(readable_dir);

    auto status = check_accessibility(readable_dir);
    EXPECT_EQ(status, AccessStatus::Accessible);
}

void demo_perms(std::filesystem::perms p) {
    using std::filesystem::perms;
    auto show = [=](char op, perms perm) {
        std::cout << (perms::none == (perm & p) ? '-' : op);
    };
    show('r', perms::owner_read);
    show('w', perms::owner_write);
    show('x', perms::owner_exec);
    show('r', perms::group_read);
    show('w', perms::group_write);
    show('x', perms::group_exec);
    show('r', perms::others_read);
    show('w', perms::others_write);
    show('x', perms::others_exec);
    std::cout << '\n';
}

TEST_F(CheckAccessibilityTest, file_insufficient_permission) {
    fs::path unreadable_file = temp_dir / "unreadable.txt";
    {
        std::ofstream ofs(unreadable_file);
    }

    demo_perms(fs::status(unreadable_file).permissions());

    fs::permissions(unreadable_file,
                    fs::perms::owner_read | fs::perms::group_read |
                        fs::perms::others_read,
                    fs::perm_options::remove);

    demo_perms(fs::status(unreadable_file).permissions());

    auto status = check_accessibility(unreadable_file);
    EXPECT_EQ(status, AccessStatus::InsufficientPermission);

    fs::permissions(unreadable_file, fs::perms::owner_read,
                    fs::perm_options::add);
}

TEST_F(CheckAccessibilityTest, directory_insufficient_permission) {
    fs::path unreadable_dir = temp_dir / "unreadable_dir";
    fs::create_directories(unreadable_dir);

    demo_perms(fs::status(unreadable_dir).permissions());
    // Remove read permissions from the owner
    fs::permissions(unreadable_dir, fs::perms::owner_read,
                    fs::perm_options::remove);

    demo_perms(fs::status(unreadable_dir).permissions());
    auto status = check_accessibility(unreadable_dir);
    EXPECT_EQ(status, AccessStatus::InsufficientPermission);

    fs::permissions(unreadable_dir, fs::perms::owner_read,
                    fs::perm_options::add);
}
