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
    bool is_base = false;

    bool operator==(RPackage const& other) const {
        return name == other.name && lib_path == other.lib_path &&
               version == other.version && dependencies == other.dependencies &&
               is_base == other.is_base;
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
        auto out = Command(R_bin)
                       .arg("-s")
                       .arg("-q")
                       .arg("-e")
                       .arg(
                           // clang-format off
                           R""(write.table(
                                     gsub(
                                       "\n",
                                       "",
                                       installed.packages()[,c("Package", "LibPath", "Version", "Depends", "Imports", "LinkingTo", "Priority")]
                                     ),
                                     sep="\U00A0",
                                     quote=FALSE,
                                     row.names=FALSE))"")
                       // clang-format on
                       .output();

        out.check_success("Unable to load R package database");

        std::istringstream stream{out.stdout_data};
        return from_stream(stream);
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

    // Return all dependencies (recursively) of the given set of packages
    // in a topologically sorted order. The packages themselves are included.
    std::vector<RPackage const*>
    get_dependencies(std::unordered_set<RPackage const*> const& pkgs) const {
        std::vector<RPackage const*> deps;
        std::unordered_set<RPackage const*> visited;
        std::unordered_set<RPackage const*> in_stack;

        for (auto p : pkgs) {
            if (!visited.count(p)) {
                dfs_visit(p, visited, in_stack, deps);
            }
        }

        std::unordered_set<RPackage const*> seen;
        std::vector<RPackage const*> result;
        result.reserve(deps.size());
        for (auto* d : deps) {
            if (!seen.count(d)) {
                seen.insert(d);
                result.push_back(d);
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

            auto tokens = string_split_n<7>(line, NBSP);
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

            bool is_base = tokens->at(6) == "base";

            auto pkg = std::make_unique<RPackage>(tokens->at(0), tokens->at(1),
                                                  tokens->at(2), dependencies,
                                                  is_base);
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

    void dfs_visit(RPackage const* pkg,
                   std::unordered_set<RPackage const*>& visited,
                   std::unordered_set<RPackage const*>& in_stack,
                   std::vector<RPackage const*>& sorted) const {
        visited.insert(pkg);
        in_stack.insert(pkg);

        for (auto& d : pkg->dependencies) {
            auto* d_pkg = find(d);
            assert(d_pkg);

            if (!visited.contains(d_pkg)) {
                dfs_visit(d_pkg, visited, in_stack, sorted);
            } else if (in_stack.contains(d_pkg)) {
                throw std::runtime_error(
                    "Cycle detected in package dependencies: " + d);
            }
        }

        in_stack.erase(pkg);
        // Post-order insertion
        sorted.push_back(pkg);
    }

    static inline Logger& log_ = LogManager::logger("rpkg-database");
    RPackages packages_;
    FileSystemTrie<RPackage const*> files_;
};

#endif // RPKG_DATABASE_
