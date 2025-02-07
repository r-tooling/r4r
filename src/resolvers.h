#ifndef RESOLVERS_H
#define RESOLVERS_H

#include "default_image_files.h"
#include "dpkg_database.h"
#include "file_tracer.h"
#include "util_fs.h"
#include "manifest.h"
#include "rpkg_database.h"
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
        for (auto const& name : index_) {
            resolvers_.at(name)->load_from_files(files);
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
    SymlinkResolver symlink_resolver{};

    packages_.clear();
    files_.clear();

    auto resolved = [&](FileInfo const& info) {
        auto path = info.path;
        for (auto const& p : symlink_resolver.resolve_symlinks(path)) {
            LOG(TRACE) << "resolving " << path;

            if (auto const* pkg = dpkg_database_->lookup_by_path(p); pkg) {
                LOG(DEBUG) << "resolved: " << path << " to: " << pkg->name;

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
              << packages_.size() << " debian packages";
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
    for (auto& f : files) {
        auto& path = f.path;
        FileStatus status = FileStatus::Copy;

        if (result_files_.contains(path)) {
            status = FileStatus::Result;
        } else if (!f.existed_before) {
            status = FileStatus::IgnoreDidNotExistBefore;
        } else if (fs::equivalent(path, cwd_)) {
            status = FileStatus::IgnoreCWD;
        } else {
            switch (check_accessibility(path)) {
            case AccessStatus::Accessible:
                status = FileStatus::Copy;
                break;
            case AccessStatus::DoesNotExist:
                status = FileStatus::IgnoreNoLongerExist;
                break;
            case AccessStatus::InsufficientPermission:
                status = FileStatus::IgnoreNotAccessible;
                break;
            }
        }

        LOG(DEBUG) << "resolved: " << path << " to: " << status;

        // TODO: check size / sha1

        files_.emplace(path, status);
    }
}

inline void CopyFileResolver::add_to_manifest(Manifest& manifest) const {
    for (auto const& [file, status] : files_) {
        manifest.add_file(file, status);
    }
}

class CRANPackageResolver : public Resolver {
  public:
    explicit CRANPackageResolver(
        std::shared_ptr<RpkgDatabase const> rpkg_database,
        std::shared_ptr<DpkgDatabase const> dpkg_database)
        : rpkg_database_{std::move(rpkg_database)},
          dpkg_database_{std::move(dpkg_database)} {}

    void load_from_files(std::vector<FileInfo>& files) override;
    void add_to_manifest(Manifest& manifest) const override;

  private:
    std::shared_ptr<RpkgDatabase const> rpkg_database_;
    std::shared_ptr<DpkgDatabase const> dpkg_database_;
    std::unordered_map<fs::path, RPackage const*> files_;
    std::unordered_set<RPackage const*> packages_;
};

inline void CRANPackageResolver::load_from_files(std::vector<FileInfo>& files) {
    SymlinkResolver symlink_resolved{};

    auto resolved = [&](FileInfo const& info) {
        auto path = info.path;
        for (auto const& p : symlink_resolved.resolve_symlinks(path)) {
            if (auto const* pkg = rpkg_database_->lookup_by_path(p); pkg) {

                LOG(DEBUG) << "resolved: " << path << " to: " << pkg->name;

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
};

inline void CRANPackageResolver::add_to_manifest(Manifest& manifest) const {
    bool needs_compilation = false;
    for (auto const* pkg : rpkg_database_->get_dependencies(packages_)) {
        if (pkg->is_base) {
            continue;
        }

        needs_compilation = needs_compilation || pkg->needs_compilation;
        manifest.add_cran_package(*pkg);
    }

    if (needs_compilation) {
        // TODO: this is a massive simplification, but in general there is no
        // way to figure what are the built requirements for a given package,
        // cf. https://github.com/r-tooling/r4r/issues/6
        for (auto const& name : {"build-essential", "r-base-dev"}) {
            auto const* pkg = dpkg_database_->lookup_by_name(name);
            if (pkg == nullptr) {
                LOG(WARN) << "Failed to find " << name
                          << " package needed "
                             "by R packages to be built from source";
            }
            manifest.add_deb_package(*pkg);
        }
    }
}

class IgnoreFileResolver : public Resolver {
  public:
    void load_from_files(std::vector<FileInfo>& files) override;

  private:
    static FileSystemTrie<ImageFileInfo> load_default_files();
    static inline std::string const kImageName = "ubuntu:22.04";

    static inline fs::path const kImageFileCache = []() {
        return get_user_cache_dir() / "r4r" / (kImageName + ".cache");
    }();

    static inline std::vector<std::string> const kBlacklistPatterns = {
        "/dev/*", "/sys/*", "/proc/*"};

    static inline FileSystemTrie<bool> kIgnoredFiles = [] {
        FileSystemTrie<bool> trie{false};
        trie.insert("/dev", true);
        trie.insert("/etc/ld.so.cache", true);
        trie.insert("/etc/nsswitch.conf", true);
        trie.insert("/etc/passwd", true);
        trie.insert("/proc", true);
        trie.insert("/sys", true);
        // created by locale-gen
        trie.insert("/usr/lib/locale/locale-archive", true);
        // fonts should be installed from a package
        trie.insert("/usr/local/share/fonts", true);
        // this might be a bit too drastic, but cache is usually not
        // transferable anyway
        trie.insert("/var/cache", true);
        return trie;
    }();
};

inline void IgnoreFileResolver::load_from_files(std::vector<FileInfo>& files) {
    static FileSystemTrie<ImageFileInfo> const kDefaultImageFiles =
        load_default_files();

    std::erase_if(files, [&](FileInfo const& info) {
        auto const& path = info.path;
        if (*kIgnoredFiles.find_last_matching(path)) {
            LOG(DEBUG) << "resolving: " << path << " to: ignored";
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
                LOG(DEBUG) << "resolving: " << path
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
                    LOG(DEBUG) << "resolving: " << path << " to: ignored";
                    return true;
                }
            }
        }
        return false;
    });
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
            LOG(WARN) << "Unable to store default image file list to "
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
