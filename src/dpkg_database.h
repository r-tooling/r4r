#ifndef DPKG_DATABASE_H
#define DPKG_DATABASE_H

#include "common.h"
#include "filesystem_trie.h"
#include "process.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
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

namespace std {

template <>
struct hash<DebPackage> {
    size_t operator()(DebPackage const& pkg) const noexcept {
        hash<string> string_hasher;
        return string_hasher(pkg.name) ^ (string_hasher(pkg.version) << 1);
    }
};

} // namespace std

using DebPackages =
    std::unordered_map<std::string, std::unique_ptr<DebPackage>>;

class DpkgDatabase {
  public:
    static DpkgDatabase system_database();
    static DpkgDatabase from_path(fs::path const& path);

    DpkgDatabase(DpkgDatabase const&) = delete;
    DpkgDatabase(DpkgDatabase&&) = default;

    DpkgDatabase& operator=(DpkgDatabase const&) = delete;

    DebPackage const* lookup_by_path(fs::path const& path) const;
    DebPackage const* lookup_by_name(std::string const& name) const;

  private:
    DpkgDatabase(DebPackages packages,
                 FileSystemTrie<DebPackage const*> files)
        : packages_{std::move(packages)}, files_{std::move(files)} {}

    DebPackages packages_;
    FileSystemTrie<DebPackage const*> files_;
};

inline DebPackages parse_installed_packages(std::istream& dpkg_output) {
    DebPackages packages;
    std::string line;

    // skip header lines
    for (int i = 0; i < 5 && std::getline(dpkg_output, line); ++i)
        ;

    while (std::getline(dpkg_output, line)) {
        std::istringstream line_stream(line);
        std::string status, name, version;

        // FIXME: USE THIS FOR PARSING THE TABLES!
        if (line_stream >> status >> std::ws >> name >> std::ws >> version) {
            if (status == "ii") { // only consider installed packages
                packages.emplace(name,
                                 std::make_unique<DebPackage>(name, version));
            }
        } else {
            // FIXME: log warning!
        }
    }

    return packages;
}

inline DebPackages load_installed_packages() {
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

inline void process_list_file(FileSystemTrie<DebPackage const*>& trie,
                              fs::path const& file, DebPackage const* pkg) {
    std::ifstream infile(file);
    if (!infile.is_open()) {
        throw std::runtime_error("Error opening file: " + file.string());
    }

    std::string line;
    while (std::getline(infile, line)) {
        if (!line.empty()) {
            trie.insert(line, pkg);
        }
    }
}

inline DpkgDatabase DpkgDatabase::system_database() {
    return DpkgDatabase::from_path("/var/lib/dpkg/info/");
}

inline DpkgDatabase DpkgDatabase::from_path(fs::path const& path) {
    FileSystemTrie<DebPackage const*> trie;

    auto packages = load_installed_packages();
    for (auto& [pkg_name, pkg] : packages) {
        auto list_file = path / (pkg_name + ".list");
        if (fs::is_regular_file(list_file)) {
            process_list_file(trie, list_file, pkg.get());
        } else {
            // FIXME: use some logging
            std::cerr << list_file << ": no such file\n";
        }
    }

    return DpkgDatabase{std::move(packages), std::move(trie)};
}

inline DebPackage const*
DpkgDatabase::lookup_by_path(fs::path const& path) const {
    auto r = files_.find(path);
    return r ? *r : nullptr;
}

inline DebPackage const*
DpkgDatabase::lookup_by_name(std::string const& name) const {
    auto it = packages_.find(name);
    return it == packages_.end() ? nullptr : it->second.get();
}

#endif