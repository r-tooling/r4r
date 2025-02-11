#ifndef DPKG_DATABASE_H
#define DPKG_DATABASE_H

#include "filesystem_trie.h"
#include "logger.h"
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

class DpkgParser {
  public:
    explicit DpkgParser(std::istream& dpkg_output)
        : dpkg_output_{dpkg_output} {}

    DebPackages parse();

  private:
    std::istream& dpkg_output_;
};

inline DebPackages DpkgParser::parse() {
    DebPackages packages;
    std::string line;

    // skip header lines
    while (std::getline(dpkg_output_, line)) {
        if (line.starts_with("+++-")) {
            break;
        }
    }

    while (std::getline(dpkg_output_, line)) {
        std::istringstream line_stream(line);
        std::string status;
        std::string name;
        std::string version;

        if (line_stream >> status >> std::ws >> name >> std::ws >> version) {
            if (status == "ii") { // only consider installed packages
                packages.emplace(name,
                                 std::make_unique<DebPackage>(name, version));
            }
        } else {
            LOG(WARN) << "Failed to parse line from dpkg: " << line;
        }
    }

    return packages;
}

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
    DpkgDatabase(DebPackages packages, FileSystemTrie<DebPackage const*> files)
        : packages_{std::move(packages)}, files_{std::move(files)} {}

    static DebPackages load_installed_packages();
    static void
    process_package_list_file(FileSystemTrie<DebPackage const*>& trie,
                              fs::path const& file, DebPackage const* pkg);

    DebPackages packages_;
    FileSystemTrie<DebPackage const*> files_;
};

inline DebPackages DpkgDatabase::load_installed_packages() {
    auto out = Command("dpkg").arg("-l").output();
    out.check_success("Unable to execute 'dpkg -l'");

    std::istringstream stream{out.stdout_data};
    DpkgParser parser{stream};
    return parser.parse();
}

inline void
DpkgDatabase::process_package_list_file(FileSystemTrie<DebPackage const*>& trie,
                                        fs::path const& file,
                                        DebPackage const* pkg) {
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
            process_package_list_file(trie, list_file, pkg.get());
        } else {
            LOG(WARN) << "Package " << pkg_name << " list file " << list_file
                      << " does not exist";
        }
    }

    return DpkgDatabase{std::move(packages), std::move(trie)};
}

inline DebPackage const*
DpkgDatabase::lookup_by_path(fs::path const& path) const {
    auto const* r = files_.find(path);
    return r != nullptr ? *r : nullptr;
}

inline DebPackage const*
DpkgDatabase::lookup_by_name(std::string const& name) const {
    auto it = packages_.find(name);
    return it == packages_.end() ? nullptr : it->second.get();
}

#endif
