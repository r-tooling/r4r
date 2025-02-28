#include "manifest_section.h"
#include <gtest/gtest.h>
#include <sstream>
#include <string>

class CopyFilesManifestSectionTest : public ::testing::Test {
  protected:
    CopyFilesManifestSection section_;
    Manifest manifest_;
};

TEST_F(CopyFilesManifestSectionTest, LoadValidLines) {
    std::string input = "C /path/one\n"
                        "R /path/two\n";
    std::istringstream iss(input);
    section_.load(iss, manifest_);

    // Check that entries were parsed correctly
    ASSERT_EQ(manifest_.copy_files.size(), 2u);
    EXPECT_EQ(manifest_.copy_files["/path/one"], FileStatus::Copy);
    EXPECT_EQ(manifest_.copy_files["/path/two"], FileStatus::Result);
}

TEST_F(CopyFilesManifestSectionTest, LoadQuotedPath) {
    std::string input = "C \" /path/with spaces \"\n";
    std::istringstream iss(input);
    section_.load(iss, manifest_);

    // Check single entry and trimmed quotes
    ASSERT_EQ(manifest_.copy_files.size(), 1u);
    EXPECT_EQ(manifest_.copy_files[" /path/with spaces "], FileStatus::Copy);
}

TEST_F(CopyFilesManifestSectionTest, LoadInvalidLines) {
    std::string input = "XYZ /path/ignored\n"     // no leading 'C' or 'R'
                        "C \"/missing/closing\n"; // incomplete quotes
    std::istringstream iss(input);
    section_.load(iss, manifest_);

    // Expect nothing to be added
    EXPECT_TRUE(manifest_.copy_files.empty());
}

TEST_F(CopyFilesManifestSectionTest, SaveValidEntries) {
    manifest_.copy_files["/path/copy"] = FileStatus::Copy;
    manifest_.copy_files["/path/result"] = FileStatus::Result;

    std::ostringstream oss;
    bool hasContent = section_.save(oss, manifest_);

    EXPECT_TRUE(hasContent);

    auto lines = string_split(oss.str(), '\n');
    EXPECT_EQ(lines[lines.size() - 2], "C /path/copy");
    EXPECT_EQ(lines[lines.size() - 1], "R /path/result");
}