#include "manifest_format.h"
#include "gtest/gtest.h"
#include <sstream>
#include <stdexcept>

TEST(ManifestFormatTest, ValidAndInvalidSectionNames) {
    EXPECT_NO_THROW({
        ManifestFormat mf;
        mf.add_section({"Section1", "content"});
    });
    EXPECT_NO_THROW({
        ManifestFormat mf;
        mf.add_section({"_section", "content"});
    });

    EXPECT_THROW(
        {
            ManifestFormat mf;
            mf.add_section({"1Section", "content"});
        },
        std::invalid_argument);

    EXPECT_THROW(
        {
            ManifestFormat mf;
            mf.add_section({"", "content"});
        },
        std::invalid_argument);
}

TEST(ManifestFormatTest, DuplicateSectionNames) {
    ManifestFormat mf;
    mf.add_section({.name = "Section", .content = "first content"});
    EXPECT_THROW(mf.add_section({"Section", "duplicate content"}),
                 std::runtime_error);
}

TEST(ManifestFormatTest, FromStreamBasic) {
    std::istringstream input(R"(
# This is a comment and should be ignored
Section1:
Line 1 of Section1
Line 2 of Section1

Section2:
Line 1 of Section2 # Inline comment should be removed
Line 2 of Section2
    )");

    ManifestFormat mf;
    input >> mf;

    auto it = mf.begin();
    ASSERT_NE(it, mf.end());
    EXPECT_EQ(it->name, "Section1");
    EXPECT_EQ(it->content, "Line 1 of Section1\nLine 2 of Section1");
    ++it;
    ASSERT_NE(it, mf.end());
    EXPECT_EQ(it->name, "Section2");
    EXPECT_EQ(it->content, "Line 1 of Section2\nLine 2 of Section2");
    ++it;
    EXPECT_EQ(it, mf.end());
}

TEST(ManifestFormatTest, WriteOutput) {
    ManifestFormat mf;
    mf.set_preamble("Manifest preamble");

    ManifestFormat::Section sec1{.name = "Section1", .content = "Line1\nLine2"};
    ManifestFormat::Section sec2{.name = "Section2",
                                 .content = "Content of Section2"};
    mf.add_section(sec1);
    mf.add_section(sec2);

    std::ostringstream oss;
    mf.write(oss);
    std::string output = oss.str();

    std::string expected = "# Manifest preamble\n\n"
                           "Section1:\n"
                           "  Line1\n"
                           "  Line2\n"
                           "\n"
                           "Section2:\n"
                           "  Content of Section2\n"
                           "\n";
    EXPECT_EQ(output, expected);
}

TEST(ManifestFormatTest, FromStreamContentBeforeHeader) {
    std::istringstream input("Content before header");
    EXPECT_THROW(
        {
            ManifestFormat format;
            input >> format;
        },
        std::runtime_error);
}
