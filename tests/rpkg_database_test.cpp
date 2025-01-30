#include "rpkg_database.h"
#include <gtest/gtest.h>

TEST(RPackagesTest, BasicParsing) {
    // Example R command output
    // note: beaware of the field separator being non-breaking-line
    // clang-format off
    char const* data =
        "askpass"   NBSP "/home/user/R/library/4.1" NBSP"1.1"   NBSP "NA"           NBSP "sys (>= 2.1)"                                                         NBSP "NA\n"
        "backports" NBSP "/home/user/R/library/4.1" NBSP"1.4.1" NBSP "R (>= 3.0.0)" NBSP "NA"                                                                   NBSP "NA\n"
        "bslib"     NBSP "/home/user/R/library/4.1" NBSP"0.4.2" NBSP "R (>= 2.10)"  NBSP "htmltools (>= 0.5.4), jsonlite, sass (>= 0.4.0),jquerylib (>= 0.1.3)" NBSP "NA\n";
    // clang-format on

    std::istringstream iss(data);
    auto packages = RpkgDatabase::from_stream(iss);

    ASSERT_EQ(packages.size(), 3u);

    // check askpass
    {
        auto pkg = packages.find("askpass");
        ASSERT_NE(pkg, nullptr);
        EXPECT_EQ(pkg->name, "askpass");
        EXPECT_EQ(pkg->lib_path, "/home/user/R/library/4.1");
        EXPECT_EQ(pkg->version, "1.1");
        ASSERT_EQ(pkg->depends.size(), 0u); // "NA" => no "depends" field
        ASSERT_EQ(pkg->imports.size(), 1u); // "sys"
        EXPECT_EQ(pkg->imports[0], "sys");
        ASSERT_EQ(pkg->linking_to.size(), 0u);
    }

    // check backports
    {
        auto pkg = packages.find("backports");
        ASSERT_NE(pkg, nullptr);
        // "R (>= 3.0.0)" => ignore "R"
        EXPECT_EQ(pkg->depends.size(), 0u);
        EXPECT_EQ(pkg->imports.size(), 0u);
    }

    // check bslib
    {
        auto pkg = packages.find("bslib");
        ASSERT_NE(pkg, nullptr);
        // "R (>= 2.10)" => ignored
        // "htmltools (>= 0.5.4), jsonlite, sass (>= 0.4.0),jquerylib (>=
        // 0.1.3)"
        // => "htmltools", "jsonlite", "sass", "jquerylib"
        ASSERT_EQ(pkg->depends.size(), 0u);
        ASSERT_EQ(pkg->imports.size(), 4u);
        EXPECT_EQ(pkg->imports[0], "htmltools");
        EXPECT_EQ(pkg->imports[1], "jsonlite");
        EXPECT_EQ(pkg->imports[2], "sass");
        EXPECT_EQ(pkg->imports[3], "jquerylib");
    }
}

// Test topological sorting
TEST(RPackagesTest, TopologicalSorting) {
    // Synthetic data
    // A depends on B, B depends on C, D no dependencies
    // So topological ordering for A is [C, B, A], for D alone is [D].
    // clang-format off
    char const* data = 
        "A" NBSP "/home/user/R/library/4.1" NBSP "1.0" NBSP "B " NBSP "NA" NBSP "NA\n"
        "B" NBSP "/home/user/R/library/4.1" NBSP "1.1" NBSP "C " NBSP "NA" NBSP "NA\n"
        "C" NBSP "/home/user/R/library/4.1" NBSP "1.2" NBSP "NA" NBSP "NA" NBSP "NA\n"
        "D" NBSP "/home/user/R/library/4.1" NBSP "1.2" NBSP "NA" NBSP "NA" NBSP "NA\n";
    // clang-format on

    std::istringstream iss(data);
    auto db = RpkgDatabase::from_stream(iss);

    {
        // If we request A, we get A plus B plus C
        std::unordered_set<std::string> pkgs = {"A"};
        auto sorted = db.get_dependencies(pkgs);
        // One valid topological order is [C, B, A]
        ASSERT_EQ(sorted.size(), 3u);
        EXPECT_EQ(sorted[0], "C");
        EXPECT_EQ(sorted[1], "B");
        EXPECT_EQ(sorted[2], "A");
    }

    {
        // If we request D, we get just D
        std::unordered_set<std::string> pkgs = {"D"};
        auto sorted = db.get_dependencies(pkgs);
        ASSERT_EQ(sorted.size(), 1u);
        EXPECT_EQ(sorted[0], "D");
    }

    {
        // If we request A and D at once, a valid topological order is:
        // [C, B, A, D] or [D, C, B, A] as long as dependencies are satisfied
        std::unordered_set<std::string> pkgs = {"A", "D"};
        auto sorted = db.get_dependencies(pkgs);
        ASSERT_EQ(sorted.size(), 4u);

        // We'll verify that C appears before B, which appears before A.
        // And D can appear anywhere as it has no dependencies.
        auto posA = std::find(sorted.begin(), sorted.end(), "A");
        auto posB = std::find(sorted.begin(), sorted.end(), "B");
        auto posC = std::find(sorted.begin(), sorted.end(), "C");
        auto posD = std::find(sorted.begin(), sorted.end(), "D");
        ASSERT_TRUE(posA != sorted.end());
        ASSERT_TRUE(posB != sorted.end());
        ASSERT_TRUE(posC != sorted.end());
        ASSERT_TRUE(posD != sorted.end());
        EXPECT_TRUE(posC < posB);
        EXPECT_TRUE(posB < posA);
    }
}
