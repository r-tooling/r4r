#ifndef RPKG_DATABASE_
#define RPKG_DATABASE_

#include "common.h"
#include "filesystem_trie.h"
#include "logger.h"
#include "process.h"
#include "util.h"
#include <cctype>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct RPackage {
    std::string name;
    fs::path lib_path;
    std::string version;
    std::unordered_set<std::string> dependencies;

    bool operator==(RPackage const& other) const {
        return name == other.name && lib_path == other.lib_path &&
               version == other.version && dependencies == other.dependencies;
    }
};

namespace std {

template <>
struct hash<RPackage> {
    size_t operator()(RPackage const& pkg) const noexcept {
        hash<string> string_hasher;
        return string_hasher(pkg.name) ^ (string_hasher(pkg.version) << 1) ^
               (string_hasher(pkg.lib_path) << 2);
    }
};

} // namespace std

using RPackages = std::unordered_map<std::string, std::unique_ptr<RPackage>>;

class RpkgDatabase {
  public:
    static RpkgDatabase from_R(fs::path const& R_bin) {
        Process R{{R_bin, "-s", "-q", "-e",
                   R""(write.table(gsub("\n", "", installed.packages()[,
             c("Package", "LibPath", "Version", "Depends", "Imports",
             "LinkingTo")]), sep="\U00A0", quote=FALSE,
             row.names=FALSE))""}};
        auto result = from_stream(R.output());
        int exit_code = R.wait();
        if (exit_code != 0) {
            throw std::runtime_error(
                STR("Unable to load R package database: R exit_code: "
                    << exit_code));
        }
        return result;
    }

    static RpkgDatabase from_stream(std::istream& input) {
        RPackages packages;
        parse_r_packages(input, packages);
        return RpkgDatabase{std::move(packages)};
    }

    RpkgDatabase(RpkgDatabase const&) = delete;
    RpkgDatabase(RpkgDatabase&&) = default;
    RpkgDatabase& operator=(RpkgDatabase const&) = delete;

    RPackage const* lookup_by_path(fs::path const& path) const {
        auto r = files_.find_last_matching(path);
        return r ? *r : nullptr;
    }

    // Return all dependencies (recursively) of the given set of packages pkgs
    // in a topologically sorted order. The packages themselves are included.
    std::vector<RPackage const*>
    get_dependencies(std::unordered_set<std::string> const& pkgs) {
        std::vector<std::string> deps;
        std::unordered_set<std::string> visited;
        std::unordered_set<std::string> in_stack;

        // Perform DFS from each of the requested packages
        for (auto const& p : pkgs) {
            if (!visited.count(p)) {
                dfs_visit(p, visited, in_stack, deps);
            }
        }

        // It might contain duplicates if multiple DFS branches visited the same
        // package. We want unique in final order (respect the first
        // occurrence).
        std::unordered_set<std::string> seen;
        std::vector<RPackage const*> result;
        result.reserve(deps.size());
        for (auto& r : deps) {
            if (!seen.count(r)) {
                seen.insert(r);
                auto& pkg = packages_.at(r);
                result.push_back(pkg.get());
            }
        }
        return result;
    }

    size_t size() const { return packages_.size(); }

    RPackage const* find(std::string const& name) const {
        auto it = packages_.find(name);
        if (it != packages_.end()) {
            return it->second.get();
        } else {
            return {};
        }
    }

  private:
    explicit RpkgDatabase(RPackages packages)
        : packages_{std::move(packages)}, files_{build_files_db(packages_)} {}

    static FileSystemTrie<RPackage const*>
    build_files_db(RPackages const& packages) {
        FileSystemTrie<RPackage const*> files{nullptr};
        for (auto const& [_, pkg] : packages) {
            files.insert(pkg->lib_path / pkg->name, pkg.get());
        }
        return files;
    }

    static void parse_r_packages(std::istream& input, RPackages& packages) {
        while (true) {
            std::string line;
            if (!std::getline(input, line)) {
                break; // no more lines
            }
            line = string_trim(line);
            if (line.empty()) {
                continue;
            }

            auto tokens = string_split_n<6>(line, NBSP);
            if (!tokens) {
                LOG_WARN(log_)
                    << "Unable to parse installed.package() output line: "
                    << line;
                continue;
            }

            std::unordered_set<std::string> dependencies;
            // Depends
            parse_dependency_field(tokens->at(3), dependencies);
            // Imports
            parse_dependency_field(tokens->at(4), dependencies);
            // LinkingTo
            parse_dependency_field(tokens->at(5), dependencies);

            auto pkg = std::make_unique<RPackage>(tokens->at(0), tokens->at(1),
                                                  tokens->at(2), dependencies);
            packages.emplace(pkg->name, std::move(pkg));
        }
    }

    // Given a single field from the line that might contain multiple
    // dependencies separated by commas, parse out the package names ignoring
    // version constraints (like "sys (>= 2.1)") and ignoring "R".
    static void
    parse_dependency_field(std::string const& field,
                           std::unordered_set<std::string>& target) {
        // If "NA", return empty
        if (field == "NA") {
            return;
        }

        std::string tmp;
        auto dep = [&]() {
            if (!tmp.empty()) {
                tmp = string_trim(tmp);
                std::string name;
                for (char x : tmp) {
                    if (x == '(' || std::isspace(x)) {
                        break;
                    }
                    name.push_back(x);
                }
                name = string_trim(name);
                if (!name.empty() && name != "R") {
                    target.insert(name);
                }
                tmp.clear();
            }
        };

        for (char c : field) {
            if (c == ',') {
                dep();
            } else {
                tmp.push_back(c);
            }
        }

        // handle the last one if any
        dep();
    }

    // Helper DFS for topological sort
    // TODO: use pointers to packages
    void dfs_visit(std::string const& name,
                   std::unordered_set<std::string>& visited,
                   std::unordered_set<std::string>& in_stack,
                   std::vector<std::string>& sorted) {
        visited.insert(name);
        in_stack.insert(name);

        auto it = packages_.find(name);
        if (it != packages_.end()) {
            auto& pkg = it->second;

            for (auto& d : pkg->dependencies) {
                // If not visited, DFS
                if (!visited.count(d)) {
                    dfs_visit(d, visited, in_stack, sorted);
                } else if (in_stack.count(d)) {
                    // Detected a cycle (d is in recursion stack)
                    // For simplicity, we throw. You could handle it differently
                    // if desired.
                    throw std::runtime_error(
                        "Cycle detected in package dependencies: " + d);
                }
            }
        }

        in_stack.erase(name);
        // Post-order insertion
        sorted.push_back(name);
    }

    static inline Logger& log_ = LogManager::logger("rpkg-database");
    RPackages packages_;
    FileSystemTrie<RPackage const*> files_;
};

#endif // RPKG_DATABASE_
