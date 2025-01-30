#include "../dpkg_database.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <unordered_map>

// Common dpkg output header for reuse in tests
// clang-format off
const std::string kDpkgHeader = R"(Desired=Unknown/Install/Remove/Purge/Hold
| Status=Not/Inst/Conf-files/Unpacked/halF-conf/Half-inst/trig-aWait/Trig-pend
|/ Err?=(none)/Reinst-required (Status,Err: uppercase=bad)
||/ Name           Version        Architecture Description
+++-==============-==============-============-=================================
)";
// clang-format on

inline DebPackages parse_installed_packages(std::string const& dpkg_output) {
    std::istringstream stream(dpkg_output);
    return parse_installed_packages(stream);
}
// Test case: No installed packages (empty dpkg output)
TEST(ParseInstalledPackagesTest, ReturnsEmptyMapForNoPackages) {
    auto packages = parse_installed_packages(kDpkgHeader);
    EXPECT_TRUE(packages.empty());
}

// Test case: Single valid package
TEST(ParseInstalledPackagesTest, ParsesSingleValidPackage) {
    std::string dpkg_output =
        kDpkgHeader +
        "ii  package1       1.0.0          all          Test package 1\n";

    auto packages = parse_installed_packages(dpkg_output);
    ASSERT_EQ(packages.size(), 1);
    EXPECT_EQ(packages.at("package1")->name, "package1");
    EXPECT_EQ(packages.at("package1")->version, "1.0.0");
}

// Test case: Multiple valid packages
TEST(ParseInstalledPackagesTest, ParsesMultipleValidPackages) {
    std::string dpkg_output =
        kDpkgHeader +
        "ii  package1       1.0.0          all          Test package 1\n"
        "ii  package2       2.3.4          all          Test package 2\n";

    auto packages = parse_installed_packages(dpkg_output);
    ASSERT_EQ(packages.size(), 2);
    EXPECT_EQ(packages.at("package1")->name, "package1");
    EXPECT_EQ(packages.at("package1")->version, "1.0.0");
    EXPECT_EQ(packages.at("package2")->name, "package2");
    EXPECT_EQ(packages.at("package2")->version, "2.3.4");
}

// Test case: Skips non-installed packages
TEST(ParseInstalledPackagesTest, SkipsNonInstalledPackages) {
    std::string dpkg_output =
        kDpkgHeader +
        "rc  package1       1.0.0          all          Test package 1\n"
        "ii  package2       2.3.4          all          Test package 2\n";

    auto packages = parse_installed_packages(dpkg_output);
    ASSERT_EQ(packages.size(), 1);
    EXPECT_EQ(packages.at("package2")->name, "package2");
    EXPECT_EQ(packages.at("package2")->version, "2.3.4");
}

// Test case: Empty input
TEST(ParseInstalledPackagesTest, ReturnsEmptyMapForEmptyInput) {
    auto packages = parse_installed_packages("");
    EXPECT_TRUE(packages.empty());
}
