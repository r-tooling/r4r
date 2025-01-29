#pragma once

#include "common.hpp"
#include "filesystem_trie.hpp"
#include "process.hpp"
#include "util.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

struct DebPackage {
    std::string name;
    std::string version;

    bool operator==(DebPackage const& other) const = default;
};

using DebPackageMap = std::unordered_map<std::string, DebPackage>;

class DpkgDatabase {
  public:
    static DpkgDatabase system_database();
    static DpkgDatabase from_path(fs::path const& path);

    DpkgDatabase(DebPackageMap packages,
                 util::FileSystemTrie<std::string> files)
        : packages_{std::move(packages)}, files_{std::move(files)} {}

    DebPackage const* lookup_by_path(fs::path const& path) const;
    DebPackage const* lookup_by_name(std::string const& name) const;

  private:
    static inline std::string const kNoPkgSentinel{"no-package-found"};

    DebPackageMap packages_;
    util::FileSystemTrie<std::string> files_;
};

inline DebPackageMap parse_installed_packages(std::istream& dpkg_output) {
    DebPackageMap package_map;
    std::string line;

    // skip header lines
    for (int i = 0; i < 5 && std::getline(dpkg_output, line); ++i)
        ;

    while (std::getline(dpkg_output, line)) {
        std::istringstream line_stream(line);
        std::string status, name, version;

        if (line_stream >> status >> std::ws >> name >> std::ws >> version) {
            if (status == "ii") { // only consider installed packages
                package_map.try_emplace(name, name, version);
            }
        }
    }

    return package_map;
}

inline DebPackageMap load_installed_packages() {
    auto [out, exit_code] = (execute_command({"dpkg", "-l"}));
    if (exit_code != 0) {
        // FIXME: create some wrapper over this pattern
        // FIXME: this is too harsh, let allow run without dpkg ?
        throw std::runtime_error(STR("Unable to execute dpkg -l, exit code:"
                                     << exit_code << "\nOutput: " << out));
    }
    std::istringstream stream{out};
    return parse_installed_packages(stream);
}

inline void process_list_file(util::FileSystemTrie<std::string>& trie,
                              fs::path const& file) {
    std::ifstream infile(file);
    if (!infile.is_open()) {
        throw std::runtime_error("Error opening file: " + file.string());
    }

    std::string package_name = file.stem().string();
    std::string line;
    while (std::getline(infile, line)) {
        if (!line.empty()) {
            trie.insert(line, package_name);
        }
    }
}

inline DpkgDatabase DpkgDatabase::system_database() {
    return DpkgDatabase::from_path("/var/lib/dpkg/info/");
}

inline DpkgDatabase DpkgDatabase::from_path(fs::path const& path) {
    util::FileSystemTrie<std::string> trie{kNoPkgSentinel};

    auto packages = load_installed_packages();
    for (auto& [pkg_name, _] : packages) {
        auto list_file = path / (pkg_name + ".list");
        if (fs::is_regular_file(list_file)) {
            process_list_file(trie, list_file);
        } else {
            // FIXME: use some logging
            std::cerr << list_file << ": no such file\n";
        }
    }

    return DpkgDatabase{packages, std::move(trie)};
}

inline DebPackage const*
DpkgDatabase::lookup_by_path(fs::path const& path) const {
    auto* pkg = files_.find(path);
    if (pkg && pkg != files_.default_value()) {
        auto it = packages_.find(*pkg);
        if (it != packages_.end()) {
            return &it->second;
        }
    }
    return {};
}

inline DebPackage const*
DpkgDatabase::lookup_by_name(std::string const& name) const {
    auto it = packages_.find(name);
    return it == packages_.end() ? nullptr : &it->second;
}

namespace std {

template <>
struct hash<DebPackage> {
    size_t operator()(DebPackage const& pkg) const noexcept {
        hash<string> string_hasher;
        return string_hasher(pkg.name) ^ (string_hasher(pkg.version) << 1);
    }
};

} // namespace std
