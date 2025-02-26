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
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class Resolver {
  public:
    using Files = std::vector<FileInfo>;

    virtual ~Resolver() = default;

    virtual void resolve(Files& files, Manifest& manifest) = 0;
};

class DebPackageResolver : public Resolver {
  public:
    explicit DebPackageResolver(DpkgDatabase const& dpkg_database)
        : dpkg_database_(dpkg_database) {}

    void resolve(Files& files, Manifest& manifest) override;

  private:
    std::reference_wrapper<DpkgDatabase const> dpkg_database_;
};

inline void DebPackageResolver::resolve(Files& files, Manifest& manifest) {
    SymlinkResolver symlink_resolver;
    std::unordered_set<DebPackage const*> resolved_packages;
    size_t resolved_files{};

    auto resolved = [&](FileInfo const& info) {
        auto& path = info.path;
        for (auto const& p : symlink_resolver.resolve_symlinks(path)) {
            if (auto const* pkg = dpkg_database_.get().lookup_by_path(p); pkg) {
                LOG(TRACE) << "Resolved: " << path << " to: " << pkg->name;

                // TODO: check that the file size is the same

                resolved_packages.insert(pkg);
                resolved_files++;
                return true;
            }
        }
        return false;
    };

    std::erase_if(files, resolved);

    LOG(INFO) << "Resolved " << resolved_files << " files to "
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
    void resolve(Files& files, Manifest& manifest) override;
};

inline void CopyFileResolver::resolve(Files& files, Manifest& manifest) {
    size_t copy_cnt = 0;
    size_t result_cnt = 0;

    std::unordered_set<fs::path> result_files_;
    for (auto& [f, s] : manifest.copy_files) {
        if (s == FileStatus::Result) {
            result_files_.insert(f);
        }
    }

    std::erase_if(files, [&](auto const& f) {
        auto& path = f.path;
        auto status = FileStatus::Copy;

        if (result_files_.contains(path)) {
            status = FileStatus::Result;
            result_cnt++;
            return true;
        }

        if (!f.existed_before) {
            if (fs::is_regular_file(f.path)) {
                status = FileStatus::Result;
            } else {
                status = FileStatus::IgnoreDidNotExistBefore;
            }
        } else if (fs::equivalent(path, manifest.cwd)) {
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
            default:
                UNREACHABLE();
            }
        }

        LOG(TRACE) << "Resolved: " << path << " to: " << status;

        // TODO: check size / sha1

        manifest.copy_files.emplace(path, status);
        return true;
    });

    LOG(INFO) << "Found " << result_cnt << " result files";
    LOG(INFO) << "Will copy " << copy_cnt << " files into the image";
}

class RPackageResolver : public Resolver {
  public:
    explicit RPackageResolver(RpkgDatabase const& rpkg_database,
                              DpkgDatabase const& dpkg_database)
        : rpkg_database_{rpkg_database}, dpkg_database_{dpkg_database} {}

    void resolve(Files& files, Manifest& manifest) override;

  private:
    std::reference_wrapper<RpkgDatabase const> rpkg_database_;
    std::reference_wrapper<DpkgDatabase const> dpkg_database_;
    void
    check_system_dependencies(std::unordered_set<RPackage const*> const& pkgs,
                              Manifest& manifest);
};

inline void RPackageResolver::resolve(Files& files, Manifest& manifest) {
    SymlinkResolver symlink_resolved;
    std::unordered_set<RPackage const*> resolved_packages;
    size_t resolved_files{};

    auto resolved = [&](FileInfo const& info) {
        auto& path = info.path;
        for (auto const& p : symlink_resolved.resolve_symlinks(path)) {
            if (auto const* pkg = rpkg_database_.get().lookup_by_path(p); pkg) {

                LOG(TRACE) << "Resolved: " << path << " to: " << pkg->name;

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

class IgnoreFileResolver : public Resolver {
  public:
    explicit IgnoreFileResolver(FileSystemTrie<bool> const&&,
                                std::string const& image_name) = delete;
    explicit IgnoreFileResolver(FileSystemTrie<bool> const& ignore_file_list,
                                std::string const& image_name)
        : ignore_file_list_{ignore_file_list}, image_name_{image_name},
          image_file_cache_(get_user_cache_dir() / "r4r" /
                            (image_name_ + ".cache")) {}

    void resolve(Files& files, Manifest& manifest) override;

  private:
    std::reference_wrapper<FileSystemTrie<bool> const> ignore_file_list_;
    FileSystemTrie<ImageFileInfo> load_default_files();
    std::string const& image_name_;

    fs::path const image_file_cache_;

    static inline std::vector<std::string> const kBlacklistPatterns = {
        "/dev/*", "/sys/*", "/proc/*"};
};

inline void IgnoreFileResolver::resolve(Files& files,
                                        [[maybe_unused]] Manifest& manifest) {
    static FileSystemTrie<ImageFileInfo> const kDefaultImageFiles =
        load_default_files();

    auto count = files.size();

    std::erase_if(files, [&](FileInfo const& info) {
        auto const& path = info.path;
        if (ignore_file_list_.get().find_last_matching(path) != nullptr) {
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
    auto default_files = [this]() {
        if (fs::exists(image_file_cache_)) {
            return DefaultImageFiles::from_file(image_file_cache_);
        }

        LOG(INFO) << "Default image file cache " << image_file_cache_
                  << " does not exists, creating from image " << image_name_;

        auto files =
            DefaultImageFiles::from_image(image_name_, kBlacklistPatterns);
        try {
            fs::create_directories(image_file_cache_.parent_path());
            std::ofstream out{image_file_cache_};
            files.save(out);
        } catch (std::exception const& e) {
            LOG(WARN) << "Failed to store default image file list to "
                      << image_file_cache_ << ": " << e.what();
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
