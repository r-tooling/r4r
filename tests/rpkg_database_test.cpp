#include "rpkg_database.h"
#include <gtest/gtest.h>
#include <unordered_set>
#include <variant>

TEST(RPackagesTest, Parsing) {
    // Example R command output
    // note: beaware of the field separator being non-breaking-line
    // clang-format off
    //  name        path                            version      <--                      dependencies                                                                 -->      priority    compilation   remote        org          name      ref
    char const* data =
        "askpass"   NBSP "/home/user/R/library/4.1" NBSP "1.1"   NBSP "NA"           NBSP "sys (>= 2.1)"                                                         NBSP "NA" NBSP "NA"   NBSP "yes"    NBSP "NA"     NBSP "NA"    NBSP "NA"    NBSP "NA"  "\n"
        "backports" NBSP "/home/user/R/library/4.1" NBSP "1.4.1" NBSP "R (>= 3.0.0)" NBSP "NA"                                                                   NBSP "NA" NBSP "NA"   NBSP "NA"     NBSP "NA"     NBSP "NA"    NBSP "NA"    NBSP "NA"  "\n"
        "bslib"     NBSP "/home/user/R/library/4.1" NBSP "0.4.2" NBSP "R (>= 2.10)"  NBSP "htmltools (>= 0.5.4), jsonlite, sass (>= 0.4.0),jquerylib (>= 0.1.3)" NBSP "NA" NBSP "NA"   NBSP "NA"     NBSP "NA"     NBSP "NA"    NBSP "NA"    NBSP "NA"  "\n"
        "tools"     NBSP "/usr/lib/R/library"       NBSP "4.1.2" NBSP "NA"           NBSP "NA"                                                                   NBSP "NA" NBSP "base" NBSP "NA"     NBSP "NA"     NBSP "NA"    NBSP "NA"    NBSP "NA"  "\n"
        "rlang"     NBSP "/usr/lib/R/library"       NBSP "0.0.1" NBSP "NA"           NBSP "NA"                                                                   NBSP "NA" NBSP "NA"   NBSP "yes"    NBSP "github" NBSP "r-lib" NBSP "rlang" NBSP "123" "\n"
        "htmltools" NBSP "/usr/lib/R/library"       NBSP "0.5.4" NBSP "NA"           NBSP "NA"                                                                   NBSP "NA" NBSP "NA"   NBSP "yes"    NBSP "github" NBSP "r-lib" NBSP "rlang" NBSP "123" "\n"
        "jsonlite"  NBSP "/usr/lib/R/library"       NBSP "0.4.0" NBSP "NA"           NBSP "NA"                                                                   NBSP "NA" NBSP "NA"   NBSP "yes"    NBSP "github" NBSP "r-lib" NBSP "rlang" NBSP "123" "\n"
        "sass"      NBSP "/usr/lib/R/library"       NBSP "0.0.1" NBSP "NA"           NBSP "NA"                                                                   NBSP "NA" NBSP "NA"   NBSP "yes"    NBSP "github" NBSP "r-lib" NBSP "rlang" NBSP "123" "\n"
        // mising jquerylib on purpose
        "sys"       NBSP "/usr/lib/R/library"       NBSP "2.1.0" NBSP "NA"           NBSP "NA"                                                                   NBSP "NA" NBSP "NA"   NBSP "yes"    NBSP "github" NBSP "r-lib" NBSP "rlang" NBSP "123" "\n";
    // clang-format on

    std::istringstream iss(data);

    RpkgDatabase packages{{}};
    auto log_sink = Logger::get().with_sink(std::make_unique<StoreSink>(), [&] {
        packages = RpkgDatabase::from_stream(iss);
    });
    auto const& msgs = log_sink->get_messages();
    ASSERT_EQ(msgs.size(), 1);
    EXPECT_EQ(msgs[0].level, LogLevel::Warning);
    EXPECT_EQ(msgs[0].message,
              "Missing dependency 'jquerylib' for package 'bslib'");

    ASSERT_EQ(packages.size(), 9);

    // check askpass
    {
        auto const* pkg = packages.find("askpass");
        ASSERT_NE(pkg, nullptr);
        EXPECT_EQ(pkg->name, "askpass");
        EXPECT_EQ(pkg->lib_path, "/home/user/R/library/4.1");
        EXPECT_EQ(pkg->version, "1.1");
        ASSERT_EQ(pkg->dependencies.size(), 1);
        EXPECT_TRUE(pkg->dependencies.contains("sys"));
        EXPECT_FALSE(pkg->is_base);
        EXPECT_TRUE(pkg->needs_compilation);
        EXPECT_TRUE(std::holds_alternative<RPackage::CRAN>(pkg->repository));
    }

    // check backports
    {
        auto const* pkg = packages.find("backports");
        ASSERT_NE(pkg, nullptr);
        // "R (>= 3.0.0)" => ignore "R"
        EXPECT_EQ(pkg->dependencies.size(), 0);
        EXPECT_FALSE(pkg->is_base);
        EXPECT_FALSE(pkg->needs_compilation);
        EXPECT_TRUE(std::holds_alternative<RPackage::CRAN>(pkg->repository));
    }

    // check bslib
    {
        auto const* pkg = packages.find("bslib");
        ASSERT_NE(pkg, nullptr);
        // "R (>= 2.10)" => ignored
        // "htmltools (>= 0.5.4), jsonlite, sass (>= 0.4.0),jquerylib (>=
        // 0.1.3)"
        // => "htmltools", "jsonlite", "sass", "jquerylib"
        ASSERT_EQ(pkg->dependencies.size(), 3);
        EXPECT_TRUE(pkg->dependencies.contains("htmltools"));
        EXPECT_TRUE(pkg->dependencies.contains("jsonlite"));
        EXPECT_TRUE(pkg->dependencies.contains("sass"));
        // missing on purpose: jquerylib
        EXPECT_FALSE(pkg->is_base);
        EXPECT_FALSE(pkg->needs_compilation);
        EXPECT_TRUE(std::holds_alternative<RPackage::CRAN>(pkg->repository));
    }

    // check base
    {
        auto const* pkg = packages.find("tools");
        ASSERT_NE(pkg, nullptr);
        EXPECT_EQ(pkg->dependencies.size(), 0);
        EXPECT_TRUE(pkg->is_base);
        EXPECT_FALSE(pkg->needs_compilation);
        EXPECT_TRUE(std::holds_alternative<RPackage::CRAN>(pkg->repository));
    }

    // check rlang
    {
        auto const* pkg = packages.find("rlang");
        ASSERT_NE(pkg, nullptr);
        EXPECT_EQ(pkg->dependencies.size(), 0);
        EXPECT_FALSE(pkg->is_base);
        EXPECT_TRUE(pkg->needs_compilation);
        EXPECT_TRUE(std::holds_alternative<RPackage::GitHub>(pkg->repository));
        auto repository = std::get<RPackage::GitHub>(pkg->repository);
        EXPECT_EQ(repository.org, "r-lib");
        EXPECT_EQ(repository.name, "rlang");
        EXPECT_EQ(repository.ref, "123");
    }
}

TEST(RPackagesTest, SystemDependencies) {
    // Synthetic data
    // RPostgres package depends on libpq-dev
    // Matrix package depends on nothing
    // UnknownPackage package does not exist
    RpkgDatabase::RPackages packages;
    packages.try_emplace("RPostgres", std::make_unique<RPackage>(
                                          RPackageBuilder("RPostgres", "1.1")
                                              .needs_compilation(true)
                                              .build()));

    packages.try_emplace(
        "Matrix", std::make_unique<RPackage>(RPackageBuilder("Matrix", "1.4-0")
                                                 .needs_compilation(true)
                                                 .build()));

    packages.try_emplace(
        "UnknownPackage",
        std::make_unique<RPackage>(RPackageBuilder("UnknownPackage", "0.1")
                                       .needs_compilation(true)
                                       .build()));

    RpkgDatabase db{std::move(packages)};

    std::unordered_set<RPackage const*> pkgs;
    pkgs.insert(db.find("RPostgres"));
    pkgs.insert(db.find("Matrix"));
    pkgs.insert(db.find("UnknownPackage"));
    auto res = RpkgDatabase::get_system_dependencies(pkgs, "ubuntu", "24.04");

    // TODO: assert no warnings
    ASSERT_EQ(res.size(), 1);
    ASSERT_TRUE(res.contains("libpq-dev"));
}

class RPackagesParameterizedTest
    : public ::testing::TestWithParam<std::tuple<
          std::vector<std::pair<std::string, std::vector<std::string>>>,
          std::vector<std::vector<std::string>>>> {};

TEST_P(RPackagesParameterizedTest, GetDependencies) {
    auto [packages, expected_plan] = GetParam();

    RpkgDatabase::RPackages pkg_map;
    for (auto const& [name, deps] : packages) {
        RPackageBuilder builder(name, "1.0");
        for (auto const& dep : deps) {
            builder.with_dependency(dep);
        }
        pkg_map.try_emplace(name, std::make_unique<RPackage>(builder.build()));
    }

    RpkgDatabase db{std::move(pkg_map)};

    std::unordered_set<RPackage const*> pkg_set;
    for (auto const& [name, _] : packages) {
        pkg_set.insert(db.find(name));
    }

    auto installation_plan = db.get_installation_plan(pkg_set);

    // Debug output
    //    std::cout << "Installation plan:\n";
    //    for (size_t i = 0; i < installation_plan.size(); ++i) {
    //        std::cout << "Level " << i << ": ";
    //        for (auto const* pkg : installation_plan[i]) {
    //            std::cout << pkg->name << " ";
    //        }
    //        std::cout << '\n';
    //    }

    ASSERT_EQ(installation_plan.size(), expected_plan.size());

    for (size_t i = 0; i < expected_plan.size(); ++i) {
        ASSERT_EQ(installation_plan[i].size(), expected_plan[i].size());
        for (size_t j = 0; j < expected_plan[i].size(); ++j) {
            EXPECT_EQ(installation_plan[i][j]->name, expected_plan[i][j]);
        }
    }
}

// clang-format off
 INSTANTIATE_TEST_SUITE_P(
     RPackagesTestInstance, RPackagesParameterizedTest,
     ::testing::Values(
         std::make_tuple(
             std::vector<std::pair<std::string, std::vector<std::string>>>{
                 {"A", {"B", "C"}},
                 {"B", {"D"}},
                 {"C", {"D"}},
                 {"D", {}}
             },
             std::vector<std::vector<std::string>>{
                 {"D"},
                 {"B", "C"},
                 {"A"}
             }
         ),
         std::make_tuple(
             std::vector<std::pair<std::string, std::vector<std::string>>>{
                 {"X", {"Y"}},
                 {"Y", {"Z"}},
                 {"Z", {}},
                 {"W", {"X", "Y"}}
             },
             std::vector<std::vector<std::string>>{
                 {"Z"},
                 {"Y"},
                 {"X"},
                 {"W"}
             }
         ),
         std::make_tuple(
             std::vector<std::pair<std::string, std::vector<std::string>>>{
                 {"P", {"Q", "R"}},
                 {"Q", {"S"}},
                 {"R", {"S"}},
                 {"S", {}},
                 {"T", {"P"}}
             },
             std::vector<std::vector<std::string>>{
                 {"S"},
                 {"Q", "R"},
                 {"P"},
                 {"T"}
             }
         )
     )
 );
// clang-format on
