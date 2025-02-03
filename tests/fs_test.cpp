#include "fs.h"
#include <gtest/gtest.h>

TEST(CheckAccessibility, BasciTest) {
    EXPECT_EQ(check_accessibility("/etc/passwd"), AccessStatus::Accessible);
    EXPECT_EQ(check_accessibility("/etc/shadow"),
              AccessStatus::InsufficientPermission);
    EXPECT_EQ(check_accessibility("/etc"), AccessStatus::Accessible);
    EXPECT_EQ(check_accessibility("/root"),
              AccessStatus::InsufficientPermission);
    EXPECT_EQ(check_accessibility("/this/path/does/not/exist"),
              AccessStatus::DoesNotExist);
}
