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
    std::vector<std::string> depends;
    std::vector<std::string> imports;
    std::vector<std::string> linking_to;

    bool operator==(RPackage const& other) const {
        return name == other.name && lib_path == other.lib_path &&
               version == other.version && depends == other.depends &&
               imports == other.imports && linking_to == other.linking_to;
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

    RPackage const* lookup_by_path(fs::path const& path) const {
        auto r = files_.find_last_matching(path);
        return r ? *r : nullptr;
    }

    // Return all dependencies (recursively) of the given set of packages pkgs
    // in a topologically sorted order. The packages themselves are included.
    std::vector<std::string>
    get_dependencies(std::unordered_set<std::string> const& pkgs) {
        std::vector<std::string> result;
        std::unordered_set<std::string> visited;
        std::unordered_set<std::string> in_stack;

        // Perform DFS from each of the requested packages
        for (auto const& p : pkgs) {
            if (!visited.count(p)) {
                dfs_visit(p, visited, in_stack, result);
            }
        }

        // It might contain duplicates if multiple DFS branches visited the same
        // package. We want unique in final order (respect the first
        // occurrence).
        std::unordered_set<std::string> seen;
        std::vector<std::string> unique_order;
        unique_order.reserve(result.size());
        for (auto& r : result) {
            if (!seen.count(r)) {
                seen.insert(r);
                unique_order.push_back(r);
            }
        }
        return unique_order;
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

    RpkgDatabase(RpkgDatabase const&) = delete;
    RpkgDatabase(RpkgDatabase&&) = default;
    RpkgDatabase& operator=(RpkgDatabase const&) = delete;

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

            auto pkg = std::make_unique<RPackage>(
                tokens->at(0), tokens->at(1), tokens->at(2),
                parse_dependency_field(tokens->at(3)),
                parse_dependency_field(tokens->at(4)),
                parse_dependency_field(tokens->at(5)));
            packages.emplace(pkg->name, std::move(pkg));
        }
    }

    // Given a single field from the line that might contain multiple
    // dependencies separated by commas, parse out the package names ignoring
    // version constraints (like "sys (>= 2.1)") and ignoring "R".
    static std::vector<std::string>
    parse_dependency_field(std::string const& field) {
        // If "NA", return empty
        if (field == "NA") {
            return {};
        }

        std::vector<std::string> result;
        std::string tmp;
        auto dep = [&]() {
            if (!tmp.empty()) {
                tmp = string_trim(tmp);
                std::string pkg_name;
                for (char x : tmp) {
                    if (x == '(' || std::isspace(x)) {
                        break;
                    }
                    pkg_name.push_back(x);
                }
                pkg_name = string_trim(pkg_name);
                if (!pkg_name.empty() && pkg_name != "R") {
                    result.push_back(pkg_name);
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

        return result;
    }

    // Helper DFS for topological sort
    // TODO: use pointers to packages
    void dfs_visit(std::string const& pkg_name,
                   std::unordered_set<std::string>& visited,
                   std::unordered_set<std::string>& in_stack,
                   std::vector<std::string>& sorted) {
        visited.insert(pkg_name);
        in_stack.insert(pkg_name);

        auto it = packages_.find(pkg_name);
        if (it != packages_.end()) {
            // Gather all direct dependencies from depends, imports, linking_to
            // TODO: make this a method
            auto const& dep_list = it->second->depends;
            auto const& imp_list = it->second->imports;
            auto const& link_list = it->second->linking_to;

            // Combine them
            std::vector<std::string> all_deps;
            all_deps.insert(all_deps.end(), dep_list.begin(), dep_list.end());
            all_deps.insert(all_deps.end(), imp_list.begin(), imp_list.end());
            all_deps.insert(all_deps.end(), link_list.begin(), link_list.end());

            for (auto const& d : all_deps) {
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

        in_stack.erase(pkg_name);
        // Post-order insertion
        sorted.push_back(pkg_name);
    }

    static inline Logger log_ = LogManager::logger("rpkg-database");
    RPackages packages_;
    FileSystemTrie<RPackage const*> files_;
};

#endif // RPKG_DATABASE_
