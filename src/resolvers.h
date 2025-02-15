#ifndef RESOLVERS_H
#define RESOLVERS_H

#include "default_image_files.h"
#include "dpkg_database.h"
#include "file_tracer.h"
#include "filesystem_trie.h"
#include "logger.h"
#include "manifest.h"
#include "rpkg_database.h"
#include "util_fs.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Resolver {
  public:
    virtual ~Resolver() = default;

    virtual void load_from_files(std::vector<FileInfo>& files) = 0;
    virtual void add_to_manifest(Manifest&) const {};
};

class Resolvers : public Resolver {
  public:
    template <std::derived_from<Resolver> T, typename... Args>

    void add(std::string const& key, Args&&... args) {
        resolvers_.emplace(key,
                           std::make_unique<T>(std::forward<Args>(args)...));
        index_.emplace_back(key);
    }

    void load_from_files(std::vector<FileInfo>& files) override {
        LOG(INFO) << "Resolving " << files.size() << " files";

        std::string summary;
        size_t total_count = files.size();

        for (auto const& name : index_) {
            size_t count = files.size();
            resolvers_.at(name)->load_from_files(files);
            summary += name + "(" + std::to_string(count - files.size()) + ") ";
        }

        LOG(INFO) << "Resolver summary: " << total_count
                  << " file(s): " << summary;

        if (files.empty()) {
            LOG(INFO) << "All files resolved";
        } else {
            LOG(INFO) << "Failed to resolve " << files.size() << " files";
            for (auto const& f : files) {
                LOG(INFO) << "Failed to resolve: " << f.path;
            }
        }
    }

    void add_to_manifest(Manifest& manifest) const override {
        for (auto const& name : index_) {
            resolvers_.at(name)->add_to_manifest(manifest);
        }
    }

  private:
    std::unordered_map<std::string, std::unique_ptr<Resolver>> resolvers_;
    std::vector<std::string> index_;
};

class DebPackageResolver : public Resolver {
  public:
    explicit DebPackageResolver(
        std::shared_ptr<DpkgDatabase const> dpkg_database)
        : dpkg_database_(std::move(dpkg_database)) {}

    void load_from_files(std::vector<FileInfo>& files) override;
    void add_to_manifest(Manifest& manifest) const override;

  private:
    std::shared_ptr<DpkgDatabase const> dpkg_database_;
    std::unordered_map<fs::path, DebPackage const*> files_;
    std::unordered_set<DebPackage const*> packages_;
};

inline void DebPackageResolver::load_from_files(std::vector<FileInfo>& files) {
    SymlinkResolver symlink_resolver;

    packages_.clear();
    files_.clear();

    auto resolved = [&](FileInfo const& info) {
        auto path = info.path;
        for (auto const& p : symlink_resolver.resolve_symlinks(path)) {
            if (auto const* pkg = dpkg_database_->lookup_by_path(p); pkg) {
                LOG(TRACE) << "Resolved: " << path << " to: " << pkg->name;

                auto it = packages_.insert(pkg);
                files_.insert_or_assign(p, *it.first);

                // TODO: check that the size is the same

                return true;
            }
        }
        return false;
    };

    std::erase_if(files, resolved);
    LOG(INFO) << "Resolved " << files_.size() << " files to "
              << packages_.size() << " deb packages";
    if (Logger::get().is_enabled(DEBUG)) {
        for (auto const* p : packages_) {
            LOG(DEBUG) << "Deb package: " << p->name << " " << p->version;
        }
    }
}

inline void DebPackageResolver::add_to_manifest(Manifest& manifest) const {
    for (auto const& pkg : packages_) {
        manifest.add_deb_package(*pkg);
    }
}

class CopyFileResolver : public Resolver {
  public:
    explicit CopyFileResolver(fs::path const& cwd,
                              AbsolutePathSet const& result_files)
        : cwd_{cwd}, result_files_{result_files} {}

    void load_from_files(std::vector<FileInfo>& files) override;
    void add_to_manifest(Manifest& manifest) const override;

  private:
    fs::path const& cwd_;
    AbsolutePathSet const& result_files_;
    std::map<fs::path, FileStatus> files_;
};

inline void CopyFileResolver::load_from_files(std::vector<FileInfo>& files) {
    size_t copy_cnt = 0;
    size_t result_cnt = 0;

    std::erase_if(files, [&](auto const& f) {
        auto& path = f.path;
        auto status = FileStatus::Copy;

        if (result_files_.contains(path)) {
            status = FileStatus::Result;
            result_cnt++;
        } else if (!f.existed_before) {
            status = FileStatus::IgnoreDidNotExistBefore;
        } else if (fs::equivalent(path, cwd_)) {
            status = FileStatus::IgnoreCWD;
        } else {
            switch (check_accessibility(path)) {
            case AccessStatus::Accessible:
                status = FileStatus::Copy;
                copy_cnt++;
                break;
            case AccessStatus::DoesNotExist:
                status = FileStatus::IgnoreNoLongerExist;
                break;
            case AccessStatus::InsufficientPermission:
                status = FileStatus::IgnoreNotAccessible;
                break;
            }
        }

        LOG(TRACE) << "Resolved: " << path << " to: " << status;

        // TODO: check size / sha1

        files_.emplace(path, status);
        return true;
    });

    LOG(INFO) << "Found " << result_cnt << " result files";
    LOG(INFO) << "Will copy " << copy_cnt << " files into the image";
}

inline void CopyFileResolver::add_to_manifest(Manifest& manifest) const {
    for (auto const& [file, status] : files_) {
        manifest.add_file(file, status);
    }
}

class RPackageResolver : public Resolver {
  public:
    explicit RPackageResolver(std::shared_ptr<RpkgDatabase const> rpkg_database,
                              std::shared_ptr<DpkgDatabase const> dpkg_database)
        : rpkg_database_{std::move(rpkg_database)},
          dpkg_database_{std::move(dpkg_database)} {}

    void load_from_files(std::vector<FileInfo>& files) override;
    void add_to_manifest(Manifest& manifest) const override;

  private:
    std::shared_ptr<RpkgDatabase const> rpkg_database_;
    std::shared_ptr<DpkgDatabase const> dpkg_database_;
    std::unordered_map<fs::path, RPackage const*> files_;
    std::set<RPackage const*> packages_;
};

inline void RPackageResolver::load_from_files(std::vector<FileInfo>& files) {
    SymlinkResolver symlink_resolved;

    auto resolved = [&](FileInfo const& info) {
        auto path = info.path;
        for (auto const& p : symlink_resolved.resolve_symlinks(path)) {
            if (auto const* pkg = rpkg_database_->lookup_by_path(p); pkg) {

                LOG(TRACE) << "Resolved: " << path << " to: " << pkg->name;

                auto it = packages_.insert(pkg);
                files_.insert_or_assign(p, *it.first);

                return true;
            }
        }
        return false;
    };

    std::erase_if(files, resolved);
    LOG(INFO) << "Resolved " << files_.size() << " files to "
              << packages_.size() << " R packages";
    if (Logger::get().is_enabled(DEBUG)) {
        for (auto const* p : packages_) {
            LOG(DEBUG) << "CRAN package: " << p->name << " " << p->version
                       << (p->needs_compilation ? " (needs compilation)" : "");
        }
    }
};

inline void RPackageResolver::add_to_manifest(Manifest& manifest) const {
    std::unordered_set<RPackage const*> compiled_packages;
    for (auto const* pkg : rpkg_database_->get_dependencies(packages_)) {
        if (pkg->is_base) {
            continue;
        }

        if (pkg->needs_compilation) {
            compiled_packages.insert(pkg);
        }

        // TODO: make repo an abstract class instead of variant, then add name
        // method
        LOG(DEBUG) << "Adding R package: " << pkg->name << " " << pkg->version
                   << (pkg->needs_compilation ? "(needs compilation)" : "");

        manifest.add_cran_package(*pkg);
    }

    if (!compiled_packages.empty()) {
        LOG(INFO) << "There are " << compiled_packages.size()
                  << " R packages that needs compilation, need to "
                     "pull system dependencies";

        auto deb_packages =
            RpkgDatabase::get_system_dependencies(compiled_packages);

        // bring in R headers and R development dependencies (includes
        // build-essential)
        deb_packages.insert("r-base-dev");

        for (auto const& name : deb_packages) {
            auto const* pkg = dpkg_database_->lookup_by_name(name);
            if (pkg == nullptr) {
                LOG(WARN) << "Failed to find " << name
                          << " package needed "
                             "by R packages to be built from source";
            } else {
                LOG(DEBUG) << "Adding native dependency: " << pkg->name << " "
                           << pkg->version;
                manifest.add_deb_package(*pkg);
            }
        }
    }
}

class IgnoreFileResolver : public Resolver {
  public:
    explicit IgnoreFileResolver(FileSystemTrie<bool> const& ignore_file_list)
        : ignore_file_list_{ignore_file_list} {}

    void load_from_files(std::vector<FileInfo>& files) override;

  private:
    FileSystemTrie<bool> const& ignore_file_list_;
    static FileSystemTrie<ImageFileInfo> load_default_files();
    static inline std::string const kImageName = "ubuntu:22.04";

    static inline fs::path const kImageFileCache = []() {
        return get_user_cache_dir() / "r4r" / (kImageName + ".cache");
    }();

    static inline std::vector<std::string> const kBlacklistPatterns = {
        "/dev/*", "/sys/*", "/proc/*"};
};

inline void IgnoreFileResolver::load_from_files(std::vector<FileInfo>& files) {
    static FileSystemTrie<ImageFileInfo> const kDefaultImageFiles =
        load_default_files();

    auto count = files.size();

    std::erase_if(files, [&](FileInfo const& info) {
        auto const& path = info.path;
        if (ignore_file_list_.find_last_matching(path) != nullptr) {
            LOG(TRACE) << "Resolving: " << path << " to: ignored";
            return true;
        }
        return false;
    });

    SymlinkResolver resolver;
    std::erase_if(files, [&](FileInfo const& info) {
        auto const& path = info.path;
        for (auto const& p : resolver.resolve_symlinks(path)) {
            if (auto const* f = kDefaultImageFiles.find(p); f) {
                // TODO: check the size, perm, ...
                LOG(TRACE) << "Resolving: " << path
                           << " to: ignored - image default";
                return true;
            }
        }
        return false;
    });

    // ignore the .uuid files from fontconfig
    static std::unordered_set<fs::path> const fontconfig_dirs = {
        "/usr/share/fonts", "/usr/share/poppler", "/usr/share/texmf/fonts"};

    std::erase_if(files, [&](FileInfo const& info) {
        auto const& path = info.path;
        for (auto const& d : fontconfig_dirs) {
            if (is_sub_path(path, d)) {
                if (path.filename() == ".uuid") {
                    LOG(TRACE) << "Resolving: " << path << " to: ignored";
                    return true;
                }
            }
        }
        return false;
    });

    LOG(INFO) << "Ignoring " << (count - files.size())
              << " of the traced files";
}

inline FileSystemTrie<ImageFileInfo> IgnoreFileResolver::load_default_files() {
    auto default_files = []() {
        if (fs::exists(kImageFileCache)) {
            return DefaultImageFiles::from_file(kImageFileCache);
        }

        LOG(INFO) << "Default image file cache " << kImageFileCache
                  << " does not exists, creating from image " << kImageName;

        auto files =
            DefaultImageFiles::from_image(kImageName, kBlacklistPatterns);
        try {
            fs::create_directories(kImageFileCache.parent_path());
            std::ofstream out{kImageFileCache};
            files.save(out);
        } catch (std::exception const& e) {
            LOG(WARN) << "Failed to store default image file list to "
                      << kImageFileCache << ": " << e.what();
        }
        return files;
    }();

    LOG(DEBUG) << "Loaded " << default_files.size() << " default files";

    FileSystemTrie<ImageFileInfo> trie;
    for (auto const& info : default_files.files()) {
        trie.insert(info.path, info);
    }
    return trie;
}

#endif // RESOLVERS_H
