#include "user.h"
#include <gtest/gtest.h>

TEST(UserInfoTest, NoThrowOnGetCurrentUserInfo) {
    // Test that get_current_user_info() does not throw.
    EXPECT_NO_THROW({ UserInfo info = UserInfo::get_current_user_info(); });
}

TEST(UserInfoTest, ValidCurrentUserInfoFields) {
    // Verify that the returned fields make sense for the current user.
    UserInfo info;
    EXPECT_NO_THROW({ info = UserInfo::get_current_user_info(); });

    // Check UID
    // There's no requirement for UID to be greater than 0 (root has UID 0),
    // so we just confirm it's not negative (unlikely on modern systems).
    EXPECT_GE(info.uid, 0);

    // Username, home directory, and shell should be non-empty on most systems,
    // but it can vary in minimal container images.
    // We'll check that these strings are not empty, which is typical.
    EXPECT_FALSE(info.username.empty())
        << "Username should not be empty for the current user.";

    EXPECT_FALSE(info.home_directory.empty())
        << "Home directory should not be empty for the current user.";

    // Some minimal systems might not have a shell set,
    // but typically it's not empty. Adjust if needed.
    EXPECT_FALSE(info.shell.empty())
        << "Shell should not be empty for the current user.";

    // Check primary group
    EXPECT_GE(info.group.gid, 0);
    EXPECT_FALSE(info.group.name.empty())
        << "Primary group name should not be empty.";

    // We can also check that the user is in at least one group (the primary
    // group). "groups" vector contains all the user’s groups. On most Unix-like
    // systems, n_groups >= 1 if successful.
    EXPECT_FALSE(info.groups.empty())
        << "The user should belong to at least the primary group.";
}

TEST(UserInfoTest, PrimaryGroupInGroupsList) {
    // Ensure that the primary group is also reflected in the 'groups' vector.
    UserInfo info = UserInfo::get_current_user_info();

    bool foundPrimary = false;
    for (auto const& g : info.groups) {
        if (g.gid == info.group.gid) {
            foundPrimary = true;
            break;
        }
    }
    EXPECT_TRUE(foundPrimary)
        << "Primary group should appear in the list of groups.";
}
