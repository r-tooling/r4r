#include "util_fs.h"
#include <gtest/gtest.h>

TEST(TempFileTest, DeletesFileOnDestruction) {
    fs::path tempPath;
    {
        TempFile temp("test_delete_", ".tmp");
        { std::ofstream ofs{*temp}; }
        tempPath = *temp;
        EXPECT_TRUE(fs::exists(tempPath))
            << "File should exist before TempFile object is destroyed";
    }
    EXPECT_FALSE(fs::exists(tempPath))
        << "File should be deleted after TempFile object is destroyed";
}

TEST(TempFileTest, KeepsFileIfDeletionDisabled) {
    fs::path temp_path;
    {
        TempFile temp("test_keep_", ".tmp", false);
        { std::ofstream ofs{*temp}; }
        temp_path = *temp;
        EXPECT_TRUE(fs::exists(temp_path))
            << "File should exist before TempFile object is destroyed";
    }
    EXPECT_TRUE(fs::exists(temp_path))
        << "File should still exist after TempFile object is destroyed";
    fs::remove(temp_path); // Cleanup manually
}

TEST(TempFileTest, GeneratesUniqueFiles) {
    TempFile temp1("unique_test_", ".tmp");
    TempFile temp2("unique_test_", ".tmp");

    EXPECT_NE(*temp1, *temp2)
        << "Generated temp files should have unique names";
}
