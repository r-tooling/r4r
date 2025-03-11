#ifndef RPKG_DATABASE_
#define RPKG_DATABASE_

#include "common.h"
#include "curl.h"
#include "filesystem_trie.h"
#include "json.h"
#include "logger.h"
#include "process.h"
#include "util.h"
#include <cctype>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

struct RPackage {
    struct GitHub {
        std::string org;
        std::string name;
        std::string ref;

        bool operator==(GitHub const& other) const {
            return std::tie(org, name, ref) ==
                   std::tie(other.org, other.name, other.ref);
        };
        friend std::ostream& operator<<(std::ostream& os, GitHub const& gh) {
            return os << "GitHub(" << gh.org << "/" << gh.name << "@" << gh.ref
                      << ")";
        }
    };

    struct CRAN {
        bool operator==(const CRAN&) const = default;

        friend std::ostream& operator<<(std::ostream& os,
                                        [[maybe_unused]] CRAN const& cran) {
            return os << "CRAN";
        }
    };

    using Repository = std::variant<GitHub, CRAN>;

    // TODO: change the order name, followd by version
    std::string name;
    fs::path lib_path;
    std::string version;
    std::set<std::string> dependencies;
    bool is_base{false};
    bool needs_compilation{false};
    Repository repository{CRAN{}};

    bool operator==(RPackage const& other) const {
        return std::tie(name, lib_path, version, dependencies, is_base,
                        needs_compilation, repository) ==
               std::tie(other.name, other.lib_path, other.version,
                        other.dependencies, other.is_base,
                        other.needs_compilation, repository);
    }
};

inline std::ostream& operator<<(std::ostream& os,
                                RPackage::Repository const& repository) {
    std::visit([&os](auto const& x) { os << x; }, repository);
    return os;
}

template <>
struct std::hash<RPackage> {
    size_t operator()(RPackage const& pkg) const noexcept {
        hash<string> string_hasher;
        return string_hasher(pkg.name) ^ (string_hasher(pkg.version) << 1) ^
               (string_hasher(pkg.lib_path) << 2);
    }
}; // namespace std

class RPackageBuilder {
  private:
    RPackage pkg_;

  public:
    RPackageBuilder(std::string name, std::string version) {
        pkg_.name = std::move(name);
        pkg_.version = std::move(version);
    };

    RPackageBuilder& lib_path(fs::path lp) {
        pkg_.lib_path = std::move(lp);
        return *this;
    }

    RPackageBuilder& with_dependency(std::string dep) {
        pkg_.dependencies.insert(std::move(dep));
        return *this;
    }

    RPackageBuilder& isBase(bool b) {
        pkg_.is_base = b;
        return *this;
    }

    RPackageBuilder& needs_compilation(bool b) {
        pkg_.needs_compilation = b;
        return *this;
    }

    RPackageBuilder& repository(RPackage::Repository repo) {
        pkg_.repository = std::move(repo);
        return *this;
    }

    [[nodiscard]] RPackage build() const { return pkg_; }
};

class RpkgDatabase {
  public:
    using RPackages =
        std::unordered_map<std::string, std::unique_ptr<RPackage>>;

    static RpkgDatabase from_R(fs::path const& R_bin);

    static RpkgDatabase from_stream(std::istream& input) {
        RPackages packages;
        parse_r_packages(input, packages);
        return RpkgDatabase{std::move(packages)};
    }

    explicit RpkgDatabase(RPackages packages)
        : packages_{std::move(packages)}, files_{build_files_db(packages_)} {
        LOG(TRACE) << "Initialized R package database with " << size()
                   << " packages (" << files_.size() << " files)";
    }

    RPackage const* lookup_by_path(fs::path const& path) const;

    static std::unordered_set<std::string>
    get_system_dependencies(std::unordered_set<RPackage const*> const& pkgs,
                            std::string const& distrib,
                            std::string const& release);

    template <typename Collection>
    std::vector<RPackage const*>
    get_dependencies(Collection const& pkg_set) const;

    template <typename Collection>
    std::vector<std::vector<RPackage const*>>
    get_installation_plan(Collection const& pkg_set) const;

    size_t size() const { return packages_.size(); }

    RPackage const* find(std::string const& name) const;

  private:
    static FileSystemTrie<RPackage const*>
    build_files_db(RPackages const& packages);

    static void parse_r_packages(std::istream& input, RPackages& packages);

    static std::optional<RPackage::GitHub>
    parse_github_repo(std::string const& org, std::string const& name,
                      std::string const& ref);

    // Given a single field from the line that might contain multiple
    // dependencies separated by commas, parse out the package names ignoring
    // version constraints (like "sys (>= 2.1)") and ignoring "R".
    static void parse_dependency_field(std::string const& field,
                                       std::set<std::string>& target);

    void
    dfs_visit(RPackage const* pkg,
              std::unordered_set<RPackage const*>& in_stack,
              std::unordered_map<RPackage const*, size_t>& dependency_levels,
              size_t& max_level) const;

    RPackages packages_;
    FileSystemTrie<RPackage const*> files_;
};

inline RpkgDatabase RpkgDatabase::from_R(fs::path const& R_bin) {
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
                                       installed.packages(
                                         fields = c(
                                           "RemoteType", "RemoteRepo", "RemoteUsername", "RemoteRef"
                                         )
                                       )[, c(
                                             "Package", "LibPath", "Version", "Depends",
                                             "Imports", "LinkingTo", "Priority", "NeedsCompilation",
                                             "RemoteType", "RemoteUsername", "RemoteRepo", "RemoteRef"
                                           )
                                        ]
                                     ),
                                     sep="\U00A0",
                                     quote=FALSE,
                                     col.names=FALSE,
                                     row.names=FALSE))"")
                   // clang-format on
                   .output();

    out.check_success("Unable to load R package database");

    std::istringstream stream{out.stdout_data};
    return from_stream(stream);
}

inline RPackage const*
RpkgDatabase::lookup_by_path(fs::path const& path) const {
    auto const* r = files_.find_last_matching(path);
    return r != nullptr ? *r : nullptr;
}

inline std::unordered_set<std::string> RpkgDatabase::get_system_dependencies(
    std::unordered_set<RPackage const*> const& pkgs, std::string const& distrib,
    std::string const& release) {
    std::unordered_set<std::string> dependencies;

    CURLMultipleTransfer<RPackage const*> curl{10};

    // Posit package manager does not support Sid of course, so we put the
    // latest Debian supported version here.
    auto posit_release =
        (distrib == "debian" && release.empty()) ? "12" : release;

    for (auto const* p : pkgs) {
        // TODO: parameterize distribution and release
        std::string url =
            STR("https://packagemanager.posit.co"
                << "/__api__/repos/"
                << "cran"
                << "/sysreqs?all=false&pkgname=" << p->name
                << "&distribution=" << distrib << "&release=" << posit_release);
        curl.add(p, url);
    }

    auto res = curl.run();

    LOG(DEBUG) << "Got system dependencies for " << res.size() << " packages";

    for (auto& [p, r] : res) {
        try {
            auto* hr = std::get_if<HttpResult>(&r);
            if (!hr) {
                throw std::runtime_error(
                    STR("Failed to query: " << std::get<std::string>(r)));
            }

            if (hr->http_code != 200) {
                throw std::runtime_error(
                    STR("Unexpected HTTP error: " << hr->http_code << "\n"
                                                  << hr->message));
            }

            auto json = JsonParser::parse(hr->message);
            auto reqs = json_query<JsonArray>(json, "requirements");
            for (auto const& req : reqs) {
                auto deps = json_query<JsonArray>(req, "requirements.packages");

                for (auto const& dep : deps) {
                    dependencies.insert(std::get<std::string>(dep));
                }
            }
        } catch (std::exception const& e) {
            LOG(WARN) << "Failed to get system dependencies for " << p->name
                      << " : " << e.what();
        }
    }

    return dependencies;
}

template <typename Collection>
std::vector<RPackage const*>
RpkgDatabase::get_dependencies(Collection const& pkg_set) const {
    auto plan = get_installation_plan(pkg_set);
    std::vector<RPackage const*> deps;
    for (auto const& step : plan) {
        deps.insert(deps.end(), step.begin(), step.end());
    }
    return deps;
}

template <typename Collection>
std::vector<std::vector<RPackage const*>>
RpkgDatabase::get_installation_plan(Collection const& pkg_set) const {
    // sort it so we have a deterministic order
    std::vector<RPackage const*> pkgs{pkg_set.begin(), pkg_set.end()};
    std::sort(pkgs.begin(), pkgs.end(),
              [](auto* a, auto* b) { return a->name < b->name; });

    std::unordered_set<RPackage const*> in_stack;
    std::unordered_map<RPackage const*, size_t> dependency_levels;

    size_t max_level = 0;
    for (auto const* p : pkgs) {
        if (!dependency_levels.contains(p)) {
            dfs_visit(p, in_stack, dependency_levels, max_level);
        }
    }

    std::vector<std::vector<RPackage const*>> installation_plan(max_level + 1);
    for (auto const& [pkg, level] : dependency_levels) {
        installation_plan[level].push_back(pkg);
    }

    for (auto& step : installation_plan) {
        std::sort(step.begin(), step.end(),
                  [](auto* a, auto* b) { return a->name < b->name; });
    }

    return installation_plan;
}

inline RPackage const* RpkgDatabase::find(std::string const& name) const {
    if (auto it = packages_.find(name); it != packages_.end()) {
        return it->second.get();
    }
    return {};
}

inline FileSystemTrie<RPackage const*>
RpkgDatabase::build_files_db(RPackages const& packages) {
    FileSystemTrie<RPackage const*> files;
    for (auto const& [_, pkg] : packages) {
        files.insert(pkg->lib_path / pkg->name, pkg.get());
    }
    return files;
}

inline void RpkgDatabase::parse_r_packages(std::istream& input,
                                           RPackages& packages) {
    while (true) {
        std::string line;
        if (!std::getline(input, line)) {
            break; // no more lines
        }
        line = string_trim(line);
        if (line.empty()) {
            continue;
        }

        auto tokens = string_split_n<12>(line, NBSP);
        if (!tokens) {
            LOG(WARN) << "Failed to parse installed.package() output line: "
                      << line;
            continue;
        }

        std::string name = tokens->at(0);
        std::string lib_path = tokens->at(1);
        std::string version = tokens->at(2);

        // we want them in order
        std::set<std::string> dependencies;
        // Depends
        parse_dependency_field(tokens->at(3), dependencies);
        // Imports
        parse_dependency_field(tokens->at(4), dependencies);
        // LinkingTo
        parse_dependency_field(tokens->at(5), dependencies);

        bool is_base = tokens->at(6) == "base";
        bool needs_compilation = tokens->at(7) == "yes";

        RPackage::Repository repo = RPackage::CRAN{};
        auto repo_type = tokens->at(8);
        if (string_iequals(repo_type, "github")) {
            std::string r_org = tokens->at(9);
            std::string r_name = tokens->at(10);
            std::string ref = tokens->at(11);

            if (auto gh = parse_github_repo(r_org, r_name, ref); gh) {
                repo = *gh;
            } else {
                continue;
            }
        }

        auto pkg =
            std::make_unique<RPackage>(name, lib_path, version, dependencies,
                                       is_base, needs_compilation, repo);
        packages.emplace(pkg->name, std::move(pkg));
    }

    // make sure we have all the dependencies
    for (auto& [_, pkg] : packages) {
        std::erase_if(pkg->dependencies, [&](auto const& dep) {
            if (!packages.contains(dep)) {
                LOG(WARN) << "Missing dependency '" << dep << "' for package '"
                          << pkg->name << "'";
                return true;
            }
            return false;
        });
    }
}

inline std::optional<RPackage::GitHub>
RpkgDatabase::parse_github_repo(std::string const& org, std::string const& name,
                                std::string const& ref) {

    std::string r = ref;

    if (org.empty() || org == "NA") {
        LOG(WARN) << "Invalid GitHub repository org for package " << name
                  << ", skipping.";
        return {};
    }
    if (name.empty() || name == "NA") {
        LOG(WARN) << "Invalid GitHub repository name for package " << name
                  << ", skipping.";
        return {};
    }
    if (ref.empty() || ref == "NA") {
        LOG(WARN) << "Invalid GitHub repository ref for package " << name
                  << " using HEAD instead";
        r = "HEAD";
    }

    return RPackage::GitHub{.org = org, .name = name, .ref = r};
}

inline void
RpkgDatabase::parse_dependency_field(std::string const& field,
                                     std::set<std::string>& target) {
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

inline void RpkgDatabase::dfs_visit(
    RPackage const* pkg, std::unordered_set<RPackage const*>& in_stack,
    std::unordered_map<RPackage const*, size_t>& dependency_levels,
    size_t& max_level) const {
    in_stack.insert(pkg);

    size_t level = 0;
    for (auto const& d_name : pkg->dependencies) {
        auto const* d_pkg = find(d_name);
        // FIXME: it should be the responsibility of the database to ensure that
        // it is consistent
        CHECK(d_pkg);

        if (in_stack.contains(d_pkg)) {
            throw std::runtime_error(
                "Cycle detected in package dependencies: " + d_name);
        }

        if (!dependency_levels.contains(d_pkg)) {
            dfs_visit(d_pkg, in_stack, dependency_levels, max_level);
        }

        auto it = dependency_levels.find(d_pkg);
        CHECK(it != dependency_levels.end());

        level = std::max(level, it->second + 1);
    }

    in_stack.erase(pkg);
    dependency_levels[pkg] = level;
    max_level = std::max(max_level, level);
}

#endif // RPKG_DATABASE_
