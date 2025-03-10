#include "dpkg_database.h"
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
    return parse_dpkg_list_output(stream);
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

TEST(ParseSourceList, ParseSourceList) {
    DebPackages packages;
    packages["idle-python3.11"] =
        std::make_unique<DebPackage>("idle-python3.11", "3.11.11-1+jammy1");
    packages["package2"] = std::make_unique<DebPackage>("package2", "1.0.0");

    std::istringstream source_list{
        R"(Package: idle-python3.11
Source: python3.11
Priority: optional
Section: python
Installed-Size: 1387
Maintainer: Matthias Klose <doko@debian.org>
Architecture: all
Version: 3.11.11-1+jammy1
Depends: python3.11, python3-tk, libjs-mathjax
Enhances: python3.11
Filename: pool/main/p/python3.11/idle-python3.11_3.11.11-1+jammy1_all.deb
Size: 407358
MD5sum: a3ba4d2e24a70a23d63fb48435f60869
SHA1: 842d6ed4a5d6f40f3cdad1d4f59931aca3909ad3
SHA256: 7971ece38b8a189c516b22decbb7c12e632db46b7c3cd608025ad752b5821bfd
Description: IDE for Python (v3.11) using Tkinter
Description-md5: 57a60fed811a55649354f3eb48ae78ff

)"};

    has_in_sources(packages, source_list);

    EXPECT_TRUE(packages.at("idle-python3.11")->in_source_list);
    EXPECT_FALSE(packages.at("package2")->in_source_list);
}

TEST(ParseSourceList, ParseSourceListArchitecture) {
    DebPackages packages;
    packages["libjson-c5:amd64"] =
        std::make_unique<DebPackage>("libjson-c5:amd64", "0.17-1build1");
    packages["package2"] = std::make_unique<DebPackage>("package2", "1.0.0");

    std::istringstream source_list{
        R"(Package: libjson-c5                                                                                         
Architecture: amd64                                                                                         
Version: 0.17-1build1                                                                                       
Multi-Arch: same                                                                                            
Priority: important                                                                                         
Section: libs                                                                                               
Source: json-c                                                                                              
Origin: Ubuntu                                                                                              
Maintainer: Ubuntu Developers <ubuntu-devel-discuss@lists.ubuntu.com>                                       
Original-Maintainer: Nicolas Mora <babelouest@debian.org>                                                   
Bugs: https://bugs.launchpad.net/ubuntu/+filebug                                                            
Installed-Size: 101                                                                                         
Depends: libc6 (>= 2.38)                                                                                    
Filename: pool/main/j/json-c/libjson-c5_0.17-1build1_amd64.deb                                              
Size: 35340                                                                                                 
MD5sum: 63671bef505df17530fda4a4d5b4649e                                                                    
SHA1: 069f16cb55a49374af57326cc423f79d213f9c65                                                              
SHA256: 3ac608d78e9e981e14ba7ceb141a46e3dd3ae8d6dbb3a7ecafd2698fe9930584                                    
SHA512: 8e5cde741b12c06e640eae932f12924649f8838436f9473f2cc721b671829bfc6f555852902ac456c340ee34722a8b98f1f2
Homepage: https://github.com/json-c/json-c/wiki                                                             
Description: JSON manipulation library - shared library                                                     
Task: cloud-minimal, minimal, server-minimal                                                                
Description-md5: ac049068ef755540cdb41614a65accbf                                                           
    )"};

    has_in_sources(packages, source_list);

    EXPECT_TRUE(packages.at("libjson-c5:amd64")->in_source_list);
    EXPECT_FALSE(packages.at("package2")->in_source_list);
}