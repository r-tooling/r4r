#include "config_file.h"
#include <gtest/gtest.h>

TEST(ConfigFileTest, ParseConfigFileFromStream) {
    std::istringstream input("key1=value1\n"
                             "key2=value2\n"
                             "# comment\n"
                             "key3=value3\n");

    ConfigFile config{input};
    EXPECT_EQ(config["key1"], "value1");
    EXPECT_EQ(config["key2"], "value2");
    EXPECT_EQ(config["key3"], "value3");
}
