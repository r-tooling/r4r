#include "ignore_file_map.h"
#include <filesystem>
#include <gtest/gtest.h>

class IgnoreFileMapTest : public ::testing::Test {
  protected:
    IgnoreFileMap map_;
};

TEST_F(IgnoreFileMapTest, WildcardIgnore) {
    map_.add_wildcard("/etc");
    EXPECT_TRUE(map_.ignore("/etc/hosts"));
    EXPECT_FALSE(map_.ignore("/usr/hosts"));
}

TEST_F(IgnoreFileMapTest, FileIgnore) {
    map_.add_file("/home/test/.config");
    EXPECT_TRUE(map_.ignore("/home/test/.config"));
    EXPECT_FALSE(map_.ignore("/home/test/.config/extra"));
}

TEST_F(IgnoreFileMapTest, CustomIgnore) {
    map_.add_custom(
        [](auto const& path) { return path.extension() == ".tmp"; });
    EXPECT_TRUE(map_.ignore("/var/folder/data.tmp"));
    EXPECT_FALSE(map_.ignore("/var/folder/data.txt"));
}
