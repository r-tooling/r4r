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
    bool in_source_list{false}; // installed from a deb file manually
                                // downloaded?

    bool operator==(DebPackage const& other) const = default;
};

template <>
struct std::hash<DebPackage> {
    size_t operator()(DebPackage const& pkg) const noexcept {
        hash<string> string_hasher;
        return string_hasher(pkg.name) ^ (string_hasher(pkg.version) << 1);
    }
};

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
    DpkgDatabase(DebPackages packages, FileSystemTrie<DebPackage const*> files)
        : packages_{std::move(packages)}, files_{std::move(files)} {}

    static DebPackages load_installed_packages();
    static void
    process_package_list_file(FileSystemTrie<DebPackage const*>& trie,
                              fs::path const& file, DebPackage const* pkg);
    static void load_source_lists(DebPackages& packages);

    DebPackages packages_;
    FileSystemTrie<DebPackage const*> files_;
};

inline DebPackages parse_dpkg_list_output(std::istream& dpkg_output) {
    DebPackages packages;
    std::string line;

    // skip header lines
    while (std::getline(dpkg_output, line)) {
        if (line.starts_with("+++-")) {
            break;
        }
    }

    while (std::getline(dpkg_output, line)) {
        std::istringstream line_stream(line);
        std::string status;
        std::string name;
        std::string version;

        if (line_stream >> status >> std::ws >> name >> std::ws >> version) {
            if (status == "ii") {
                // only consider installed packages
                packages.emplace(name,
                                 std::make_unique<DebPackage>(name, version));
            }
        } else {
            LOG(WARN) << "Failed to parse line from dpkg: " << line;
        }
    }

    return packages;
}

inline DebPackages DpkgDatabase::load_installed_packages() {
    auto const out = Command("dpkg").arg("-l").output();
    out.check_success("Unable to execute 'dpkg -l'");

    std::istringstream stream{out.stdout_data};
    return parse_dpkg_list_output(stream);
}

// This assumes an uncompressed _Packages file
inline void has_in_sources(DebPackages& packages, std::istream& source_list) {
    std::string line;
    while (std::getline(source_list, line)) {
        if (line.starts_with("Package: ")) {
            std::string name = line.substr(9);
            auto it = packages.find(name);
            if (it != packages.end()) {
                // check the version in the source_list
                // Version is the 8th line after Package
                for (int i = 0; i < 7; i++) {
                    std::getline(source_list, line);
                }
                if (line.starts_with("Version: ")) {
                    std::string version = line.substr(9);
                    if (version == it->second->version) {
                        it->second->in_source_list = true;
                    }
                } else {
                    LOG(FATAL)
                        << "Failed to parse version in source list for package "
                        << name;
                }
            }
        }
    }
}

inline void DpkgDatabase::load_source_lists(DebPackages& packages) {
    auto const sources_list_dir = fs::path("/var/lib/apt/lists/");
    for (auto const& entry : fs::directory_iterator(sources_list_dir)) {
        // The entry finished by _Packages, possibly followed by .gz, .lz4, .xz
        std::regex ansi_regex(R"(.+_Packages)(\.(gz|lz4|xz))?$)");

        auto const filename = entry.path().filename().string();
        if (std::regex_match(filename, ansi_regex)) {
            // Decompress to stdout if it is compressed
            auto const path = entry.path().string();
            std::unique_ptr<std::istream> source_list;
            if (filename.ends_with(".gz")) {
                auto const out = Command("gunzip").arg(path).arg("-c").output();
                out.check_success("Unable to execute 'gunzip'");
                source_list =
                    std::make_unique<std::istringstream>(out.stdout_data);
            } else if (filename.ends_with(".lz4")) {
                auto const out = Command("lz4").arg(path).arg("-cd").output();
                out.check_success("Unable to execute 'lz4'");
                source_list =
                    std::make_unique<std::istringstream>(out.stdout_data);
            } else if (filename.ends_with(".xz")) {
                // lzcat is equivalent to xz --decompress --stdout
                auto const out = Command("xzcat").arg(path).output();
                out.check_success("Unable to execute 'xzcat'");
                source_list =
                    std::make_unique<std::istringstream>(out.stdout_data);
            } else {
                std::ifstream source_list_file(path);
                if (!source_list_file.is_open()) {
                    throw std::runtime_error("Error opening file: " + path);
                }
                source_list = std::make_unique<std::ifstream>(
                    std::move(source_list_file));
            };

            has_in_sources(packages, *source_list);
        }
    }

    // Remove all the packages that are not in a source list
    for (auto it = packages.begin(); it != packages.end();) {
        if (!it->second->in_source_list) {
            it = packages.erase(it);
        } else {
            ++it;
        }
    }
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
    load_source_lists(packages);
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

constexpr std::string_view kDpkgArch =
#if defined(__x86_64__) || defined(_M_X64)
    "amd64";
#else
    "";
#endif

inline DebPackage const*
DpkgDatabase::lookup_by_name(std::string const& name) const {

    auto it = packages_.find(name);

    if (it == packages_.end()) {
        // try multiarch name
        it = packages_.find(STR(name << ":" << kDpkgArch));
    }

    return it == packages_.end() ? nullptr : it->second.get();
}

#endif
