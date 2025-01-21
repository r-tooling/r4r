#include "dpkgResolver.hpp"
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

// FIXME: rename file
namespace backend {

static const std::string NO_PKG_SENTINEL {"NO_PKG_SENTINEL"};

std::unordered_map<std::string, DebPackage> load_installed_packages() {
    std::unordered_map<std::string, DebPackage> package_map;

    auto dpkg_output = ::util::execute_command("dpkg -l");
    std::istringstream stream(dpkg_output);
    std::string line;

    // skip header lines
    for (int i = 0; i < 5 && std::getline(stream, line); ++i)
        ;

    while (std::getline(stream, line)) {
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

void process_list_file(util::FilesystemTrie<std::string>& trie,
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

DpkgDatabase DpkgDatabase::from_path(fs::path const& path) {
    util::FilesystemTrie<std::string> trie{NO_PKG_SENTINEL};

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

DebPackage const* DpkgDatabase::lookup_by_path(fs::path const& path) const {
    auto* pkg = files_.find(path);
    if (pkg) {
        auto it = packages_.find(*pkg);
        if (it != packages_.end()) {
            return &it->second;
        }
    }
    return {};
}

DebPackage const* DpkgDatabase::lookup_by_name(std::string const& name) const {
    auto it = packages_.find(name);
    return it == packages_.end() ? nullptr : &it->second;
}

} // namespace backend
