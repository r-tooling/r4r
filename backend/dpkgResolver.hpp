#pragma once

// FIXME: do not use this relative includes
#include "../common.hpp"
#include "../util.hpp"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>

namespace backend {

struct DebPackage {
    std::string name;
    std::string version;
};

class DpkgDatabase {
  public:
    static DpkgDatabase from_path(fs::path const& path = "/var/lib/dpkg/info/");

    DpkgDatabase(std::unordered_map<std::string, DebPackage> packages,
                 util::FilesystemTrie<std::string> files)
        : packages_{std::move(packages)}, files_{std::move(files)} {}

    DebPackage const* lookup(fs::path const& path) const;

  private:
    std::unordered_map<std::string, DebPackage> packages_;
    util::FilesystemTrie<std::string> files_;
};

} // namespace backend
