#include "manifest_section.h"
#include <gtest/gtest.h>
#include <sstream>
#include <string>

class RPackageManifestSectionTest : public ::testing::Test {
  protected:
    RPackagesManifestSection section_;
    Manifest manifest_;
};

TEST_F(RPackageManifestSectionTest, SaveValidEntries) {
    manifest_.r_packages.insert(new RPackage{"testpkg",
                                             "/path/to/lib",
                                             "1.0",
                                             {"dep1", "dep2"},
                                             false,
                                             false,
                                             RPackage::CRAN{}});
    manifest_.r_packages.insert(
        new RPackage("", "/path/result", "1.0", {}, false, true,
                     RPackage::GitHub{"org", "name", "ref"}));

    std::ostringstream oss;
    bool hasContent = section_.save(oss, manifest_);

    EXPECT_TRUE(hasContent);

    auto lines = string_split(oss.str(), '\n');
    EXPECT_EQ(lines[lines.size() - 2], "I github org/name@ref");
    EXPECT_EQ(lines[lines.size() - 1], "I cran testpkg");
}