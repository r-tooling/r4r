#ifndef RESOLVERS_H
#define RESOLVERS_H

#include "dpkg_database.h"
#include "file_tracer.h"
#include "logger.h"
#include "manifest.h"
#include "rpkg_database.h"
#include "util_fs.h"
#include <filesystem>
#include <system_error>
#include <vector>

class Resolver {
  public:
    using Files = std::vector<FileInfo>;
    using Symlinks = std::map<fs::path, fs::path>;

    virtual ~Resolver() = default;

    virtual void resolve(Files& files, Symlinks& symlinks,
                         Manifest& manifest) = 0;
};

class DebPackageResolver : public Resolver {
  public:
    explicit DebPackageResolver(DpkgDatabase const* dpkg_database)
        : dpkg_database_(dpkg_database) {}

    void resolve(Files& files, Symlinks& symlinks, Manifest& manifest) override;

  private:
    DpkgDatabase const* dpkg_database_;
};

inline void DebPackageResolver::resolve(Files& files, Symlinks& symlinks,
                                        Manifest& manifest) {
    SymlinkResolver symlink_resolver;
    std::unordered_set<DebPackage const*> resolved_packages;
    size_t resolved_files{};

    auto resolved = [&](fs::path const& path) {
        for (auto const& p : symlink_resolver.resolve_symlinks(path)) {
            if (!fs::is_regular_file(p)) {
                LOG(DEBUG) << "Skipping: " << path
                           << " as it is not a regular file ("
                           << file_type_str(path) << ")";
            }
            if (auto const* pkg = dpkg_database_->lookup_by_path(p); pkg) {
                if (pkg->name.find("rstudio") != std::string::npos ||
                    pkg->name.find("bslib") != std::string::npos) {
                    //  TODO: GET RID OF THIS!! THIS IS A HACK TO MAKE TRACING
                    //  PASS FROM RSTUDIO
                    continue;
                }

                LOG(DEBUG) << "Resolved: " << path << " to: " << pkg->name;

                // TODO: check that the file size is the same

                resolved_packages.insert(pkg);
                resolved_files++;
                return true;
            }
        }
        return false;
    };

    std::erase_if(files, [&](auto const& info) { return resolved(info.path); });
    std::erase_if(symlinks,
                  [&](auto const& entry) { return resolved(entry.first); });

    LOG(INFO) << "Resolved " << resolved_files << " files and symlinks to "
              << resolved_packages.size() << " deb packages";

    if (Logger::get().is_enabled(DEBUG)) {
        for (auto const* p : resolved_packages) {
            LOG(DEBUG) << "Deb package: " << p->name << " " << p->version;
        }
    }

    manifest.deb_packages.merge(resolved_packages);
}

class CopyFileResolver : public Resolver {
  public:
    void resolve(Files& files, Symlinks& symlinks, Manifest& manifest) override;
};

inline void CopyFileResolver::resolve(Files& files, Symlinks& symlinks,
                                      Manifest& manifest) {
    size_t copy_cnt = 0;
    size_t result_cnt = 0;

    std::unordered_set<fs::path> result_files_;
    for (auto& [f, s] : manifest.copy_files) {
        if (s == FileStatus::Result) {
            result_files_.insert(f);
        }
    }

    // TODO: split to two smaller functions
    LOG(DEBUG) << "Resolving files";
    std::erase_if(files, [&](FileInfo const& f) {
        auto& path = f.path;
        auto status = FileStatus::Copy;

        if (result_files_.contains(path)) {
            status = FileStatus::Result;
            result_cnt++;
            return true;
        }

        try {
            switch (check_accessibility(path)) {
            case AccessStatus::Accessible: {
                bool shoud_consider = fs::is_regular_file(path);

                if (fs::is_symlink(path)) {
                    if (auto it = resolve_symlink(path); it) {
                        shoud_consider = fs::is_regular_file(*it);
                    }
                }

                if (shoud_consider) {
                    if (f.existed_before) {
                        status = FileStatus::Copy;
                        copy_cnt++;
                    } else {
                        status = FileStatus::Result;
                        result_cnt++;
                    }
                } else {
                    status = FileStatus::IgnoreDirectory;
                }
                break;
            }
            case AccessStatus::DoesNotExist:
                status = FileStatus::IgnoreNoLongerExist;
                break;
            case AccessStatus::InsufficientPermission:
                status = FileStatus::IgnoreNotAccessible;
                break;
            default:
                UNREACHABLE();
            }
        } catch (fs::filesystem_error const& e) {
            LOG(WARN) << "Failed to check file status: " << f.path << " - "
                      << e.what();
            status = FileStatus::IgnoreNotAccessible;
        }

        LOG(DEBUG) << "Resolved: " << path << " to: " << status;

        // TODO: check size / sha1

        manifest.copy_files.emplace(path, status);
        return true;
    });

    LOG(DEBUG) << "Resolving symlinks";

    std::erase_if(symlinks, [&](auto const& entry) {
        std::error_code ec;
        auto is_link = fs::is_symlink(entry.first, ec);
        if (ec) {
            LOG(WARN) << "Failed to check symlink " << entry.first << " - "
                      << ec.message();
            return false;
        }

        if (!is_link) {
            LOG(WARN) << "Traced symlink " << entry.first
                      << " is not a symlink anymore";
            return false;
        }

        auto exits = fs::exists(entry.second, ec);
        if (ec) {
            LOG(WARN) << "Failed to check file " << entry.second << " - "
                      << ec.message();
            return false;
        }

        if (!exits) {
            LOG(DEBUG) << "Traced symlink " << entry.first << " target "
                       << entry.second << " no longer exists";
            return false;
        }

        LOG(DEBUG) << "Adding symlink " << entry.first;

        manifest.symlinks.insert(entry.first);
        return true;
    });

    // TODO: out of how many result files
    LOG(INFO) << "Found " << result_cnt << " result files";
    LOG(INFO) << "Will copy " << copy_cnt << " files into the image";
    LOG(INFO) << "Will install " << manifest.symlinks.size() << " symlinks";
}

class RPackageResolver : public Resolver {
  public:
    explicit RPackageResolver(RpkgDatabase const* rpkg_database)
        : rpkg_database_{rpkg_database} {}

    void resolve(Files& files, Symlinks& symlinks, Manifest& manifest) override;

  private:
    RpkgDatabase const* rpkg_database_;
    void
    check_system_dependencies(std::unordered_set<RPackage const*> const& pkgs,
                              Manifest& manifest);
};

inline void RPackageResolver::resolve(Files& files, Symlinks& /*symlinks*/,
                                      Manifest& manifest) {
    SymlinkResolver symlink_resolved;
    std::unordered_set<RPackage const*> resolved_packages;
    size_t resolved_files{};

    auto resolved = [&](FileInfo const& info) {
        auto& path = info.path;
        for (auto const& p : symlink_resolved.resolve_symlinks(path)) {
            if (auto const* pkg = rpkg_database_->lookup_by_path(p); pkg) {

                LOG(DEBUG) << "Resolved: " << path << " to: " << pkg->name;

                resolved_packages.insert(pkg);
                resolved_files++;
                return true;
            }
        }
        return false;
    };

    std::erase_if(files, resolved);

    LOG(INFO) << "Resolved " << resolved_files << " files to "
              << resolved_packages.size() << " R packages";

    if (Logger::get().is_enabled(DEBUG)) {
        for (auto const* p : resolved_packages) {
            // TODO: add repo info
            LOG(DEBUG) << "R package: " << p->name << " " << p->version
                       << " from " << p->repository;
        }
    }

    manifest.r_packages.merge(resolved_packages);
}

#endif // RESOLVERS_H
