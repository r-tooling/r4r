#pragma once

// FIXME: do not use this relative includes
#include "../common.hpp"
#include "../filesystemtrie.hpp"
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

namespace backend {

struct DebPackage {
    std::string name;
    std::string version;

    bool operator==(const DebPackage& other) const = default;
};

class DpkgDatabase {
  public:
    static DpkgDatabase from_path(fs::path const& path = "/var/lib/dpkg/info/");

    DpkgDatabase(std::unordered_map<std::string, DebPackage> packages,
                 util::FileSystemTrie<std::string> files)
        : packages_{std::move(packages)}, files_{std::move(files)} {}

    DebPackage const* lookup_by_path(fs::path const& path) const;
    DebPackage const* lookup_by_name(std::string const& name) const;

  private:
    std::unordered_map<std::string, DebPackage> packages_;
    util::FileSystemTrie<std::string> files_;
};

} // namespace backend

namespace std {

template <>
struct hash<backend::DebPackage> {
    size_t operator()(const backend::DebPackage& pkg) const noexcept {
        hash<string> string_hasher;
        return string_hasher(pkg.name) ^ (string_hasher(pkg.version) << 1);
    }
};

} // namespace std
