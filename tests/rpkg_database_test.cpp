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
        ASSERT_EQ(pkg->dependencies.size(), 1u);
        EXPECT_TRUE(pkg->dependencies.contains("sys"));
    }

    // check backports
    {
        auto pkg = packages.find("backports");
        ASSERT_NE(pkg, nullptr);
        // "R (>= 3.0.0)" => ignore "R"
        EXPECT_EQ(pkg->dependencies.size(), 0u);
    }

    // check bslib
    {
        auto pkg = packages.find("bslib");
        ASSERT_NE(pkg, nullptr);
        // "R (>= 2.10)" => ignored
        // "htmltools (>= 0.5.4), jsonlite, sass (>= 0.4.0),jquerylib (>=
        // 0.1.3)"
        // => "htmltools", "jsonlite", "sass", "jquerylib"
        ASSERT_EQ(pkg->dependencies.size(), 4u);
        EXPECT_TRUE(pkg->dependencies.contains("htmltools"));
        EXPECT_TRUE(pkg->dependencies.contains("jsonlite"));
        EXPECT_TRUE(pkg->dependencies.contains("sass"));
        EXPECT_TRUE(pkg->dependencies.contains("jquerylib"));
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
        auto deps = db.get_dependencies(pkgs);
        // One valid topological order is [C, B, A]
        ASSERT_EQ(deps.size(), 3u);
        EXPECT_EQ(deps[0]->name, "C");
        EXPECT_EQ(deps[1]->name, "B");
        EXPECT_EQ(deps[2]->name, "A");
    }

    {
        // If we request D, we get just D
        std::unordered_set<std::string> pkgs = {"D"};
        auto deps = db.get_dependencies(pkgs);
        ASSERT_EQ(deps.size(), 1u);
        EXPECT_EQ(deps[0]->name, "D");
    }

    {
        // If we request A and D at once, a valid topological order is:
        // [C, B, A, D] or [D, C, B, A] as long as dependencies are satisfied
        std::unordered_set<std::string> pkgs = {"A", "D"};
        auto deps = db.get_dependencies(pkgs);
        ASSERT_EQ(deps.size(), 4u);

        // We'll verify that C appears before B, which appears before A.
        // And D can appear anywhere as it has no dependencies.
        auto posA = std::find_if(deps.begin(), deps.end(),
                                 [](auto* p) { return p->name == "A"; });
        auto posB = std::find_if(deps.begin(), deps.end(),
                                 [](auto* p) { return p->name == "B"; });
        auto posC = std::find_if(deps.begin(), deps.end(),
                                 [](auto* p) { return p->name == "C"; });
        auto posD = std::find_if(deps.begin(), deps.end(),
                                 [](auto* p) { return p->name == "D"; });
        ASSERT_TRUE(posA != deps.end());
        ASSERT_TRUE(posB != deps.end());
        ASSERT_TRUE(posC != deps.end());
        ASSERT_TRUE(posD != deps.end());
        EXPECT_TRUE(posC < posB);
        EXPECT_TRUE(posB < posA);
    }
}
