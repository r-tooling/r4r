#ifndef MANIFEST_H
#define MANIFEST_H

#include "archive.h"
#include "cli.h"
#include "default_image_files.h"
#include "dockerfile.h"
#include "dpkg_database.h"
#include "file_tracer.h"
#include "fs.h"
#include "logger.h"
#include "rpkg_database.h"
#include <algorithm>
#include <bits/types/struct_sched_param.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

class ManifestFormat {
  public:
    static constexpr char comment() noexcept { return kCommentChar; }

    struct Section {
        std::string name;
        std::string content{};
        std::string preamble{};
    };

    ManifestFormat() = default;

    void preamble(std::string preamble) { preamble_ = std::move(preamble); }

    void add_section(Section const& section) {
        if (!is_valid_section_name(section.name)) {
            throw std::invalid_argument("Invalid section name: " +
                                        section.name);
        }
        if (std::find_if(sections_.begin(), sections_.end(),
                         [&](Section const& x) {
                             return x.name == section.name;
                         }) != sections_.end()) {
            throw std::runtime_error(
                STR("section: " << section.name << " already exists"));
        }
        sections_.push_back(section);
    }

    using iterator = std::vector<Section>::iterator;
    using const_iterator = std::vector<Section>::const_iterator;

    iterator begin() { return sections_.begin(); }
    iterator end() { return sections_.end(); }
    const_iterator begin() const { return sections_.begin(); }
    const_iterator end() const { return sections_.end(); }
    const_iterator cbegin() const { return sections_.cbegin(); }
    const_iterator cend() const { return sections_.cend(); }

    static ManifestFormat from_stream(std::istream& in) {
        ManifestFormat manifest;
        std::string line;
        Section* section = nullptr;

        while (std::getline(in, line)) {
            if (auto pos = line.find(kCommentChar); pos != std::string::npos) {
                line = line.substr(0, pos);
            }

            line = string_trim(line);
            if (line.empty()) {
                continue;
            }
            if (is_section_header(line)) {
                std::string name = line.substr(0, line.size() - 1);
                manifest.add_section({name});
                section = &manifest.sections_.back();
            } else {
                if (!section) {
                    throw std::runtime_error(
                        "Content line encountered before any section header: " +
                        line);
                }
                if (!section->content.empty()) {
                    section->content.push_back('\n');
                }
                section->content.append(line);
            }
        }

        return manifest;
    }

    void write(std::ostream& out) const {
        if (!preamble_.empty()) {
            prefixed_ostream(out, "# ", [&] { out << preamble_; });
            out << "\n\n";
        }

        for (auto& [name, content, preamble] : sections_) {
            if (!preamble.empty()) {
                prefixed_ostream(out, "# ", [&] { out << preamble; });
                out << '\n';
            }
            out << name << ':' << '\n';
            prefixed_ostream(out, "  ", [&] { out << content; });
            out << "\n\n";
        }
    }

  private:
    static constexpr char kCommentChar = '#';

    std::string preamble_;
    std::vector<Section> sections_;

    friend std::ostream& operator<<(std::ostream& os,
                                    ManifestFormat const& format) {
        format.write(os);
        return os;
    }

    static bool is_valid_section_name(std::string_view name) {
        if (name.empty()) {
            return false;
        } else if (!std::isalpha(name[0]) && name[0] != '_') {
            return false;
        } else {
            for (char ch : name) {
                if (!std::isalnum(ch) && ch != '_') {
                    return false;
                }
            }
        }
        return true;
    }

    static bool is_section_header(std::string_view line) {
        if (line.empty() || line.back() != ':') {
            return false;
        }
        auto name = line.substr(0, line.size() - 1);
        return is_valid_section_name(name);
    }
};

class ManifestPart {
  public:
    virtual ~ManifestPart() = default;

    virtual void load_from_files(std::vector<FileInfo>&) = 0;
    virtual void load_from_manifest(std::istream&){};
    virtual void write_to_manifest(ManifestFormat::Section&) const = 0;
    virtual void write_to_docker(DockerFileBuilder&) const {};

  protected:
    static bool is_section_header(std::string const& line) {
        static std::regex const section_regex(R"(^\w+:$)");
        return std::regex_match(line, section_regex);
    }
};

class DebPackagesManifest : public ManifestPart {
  public:
    explicit DebPackagesManifest(
        DpkgDatabase dpkg_database = DpkgDatabase::system_database())
        : dpkg_database_(std::move(dpkg_database)) {}

    void load_from_files(std::vector<FileInfo>& files) override {
        SymlinkResolver symlink_resolver{};

        packages_.clear();
        files_.clear();

        auto resolved = [&](FileInfo const& info) {
            auto path = info.path;
            for (auto& p : symlink_resolver.resolve_symlinks(path)) {
                LOG_TRACE(log_) << "resolving " << path;

                if (auto* pkg = dpkg_database_.lookup_by_path(p); pkg) {
                    LOG_DEBUG(log_)
                        << "resolved: " << path << " to: " << pkg->name;

                    auto it = packages_.insert(pkg);
                    files_.insert_or_assign(p, *it.first);

                    // TODO: check that the size is the same

                    return true;
                }
            }
            return false;
        };

        std::erase_if(files, resolved);
        LOG_INFO(log_) << "Resolved " << files_.size() << " files to "
                       << packages_.size() << " debian packages";
    };

    void load_from_manifest(std::istream& input) override {
        packages_.clear();
        files_.clear();

        std::string line;

        while (std::getline(input, line)) {
            line = string_trim(line);
            if (!line.starts_with("-")) {
                throw std::runtime_error(
                    "Unable to parse package specification: " + line);
            }
            std::istringstream line_stream(line.substr(1)); // Skip '-'
            std::string name, version;
            line_stream >> name >> version;

            name = string_trim(name);
            version = string_trim(version);

            if (name.empty() || version.empty()) {
                throw std::runtime_error("Invalid package format: " + line);
            }

            auto* pkg = dpkg_database_.lookup_by_name(name);
            if (pkg) {
                if (pkg->version != version) {
                    LOG_WARN(log_) << "Package version mismatch: " << name
                                   << " manifest: " << version
                                   << " installed: " << pkg->version;
                } else {
                    packages_.insert(pkg);
                }
            } else {
                LOG_WARN(log_) << "Package not found: " << name;
            }
        }

        LOG_INFO(log_) << "Loaded " << packages_.size()
                       << " packages from manifest";
    }

    void write_to_manifest(ManifestFormat::Section& section) const override {
        if (packages_.empty()) {
            section.preamble = "No system packages will be installed";
            return;
        }

        section.preamble =
            STR("The following " << packages_.size()
                                 << " packages will be installed:");
        std::ostringstream content;
        for (auto const& pkg : packages_) {
            content << "- " << pkg->name << " " << pkg->version << "\n";
        }
        section.content = content.str();
    }

    void write_to_docker(DockerFileBuilder& builder) const override {

        // FIXME: the main problem with this implementation is that
        // it ignores the fact a package can come from multiple repos
        // and that these repos need to be installed.

        if (packages_.empty()) {
            return;
        }

        std::vector<std::string> pkgs;
        pkgs.reserve(packages_.size());
        for (auto pkg : packages_) {
            pkgs.push_back(pkg->name + "=" + pkg->version);
        }
        std::sort(pkgs.begin(), pkgs.end());

        std::string pkgs_line = string_join(pkgs, " \\\n      ");

        std::vector<std::string> cmds;
        cmds.push_back("apt-get update -y");
        cmds.push_back("apt-get install -y --no-install-recommend " +
                       pkgs_line);

        builder.run(cmds);
    };

  private:
    static inline Logger& log_ = LogManager::logger("manifest.dpkg");

    DpkgDatabase dpkg_database_;
    std::unordered_map<fs::path, DebPackage const*> files_;
    std::unordered_set<DebPackage const*> packages_;
};

class CRANPackagesManifest : public ManifestPart {
  public:
    explicit CRANPackagesManifest(RpkgDatabase rpkg_database,
                                  fs::path const& output_dir)
        : rpkg_database_(std::move(rpkg_database)),
          script_{output_dir / "install_r_packages.R"} {}

    explicit CRANPackagesManifest(fs::path const& R_bin,
                                  fs::path const& output_dir)
        : CRANPackagesManifest(RpkgDatabase::from_R(R_bin), output_dir) {}

    void load_from_files(std::vector<FileInfo>& files) override;
    void write_to_manifest(ManifestFormat::Section& section) const override;
    void write_to_docker(DockerFileBuilder& builder) const override;

  private:
    static inline Logger& log_ = LogManager::logger("manifest.rpkg");

    RpkgDatabase rpkg_database_;
    std::unordered_map<fs::path, RPackage const*> files_;
    std::unordered_set<RPackage const*> packages_;
    fs::path script_;
};

inline void
CRANPackagesManifest::write_to_docker(DockerFileBuilder& builder) const {
    if (packages_.empty()) {
        return;
    }

    std::ofstream script(script_);
    // TODO: parameterize the max cores
    script << "cores <- min(parallel::detectCores(), 4)\n"
           << "tmp_dir <- tempdir()\n"
           << "install.packages('remotes', lib = c(tmp_dir))\n"
           << "require('remotes', lib.loc = c(tmp_dir))\n"
           << "on.exit(unlink(tmp_dir, recursive = TRUE))\n"
           << "\n"
           << "# installing packages\n\n";

    // we have to install the dependencies ourselves otherwise we cannot get
    // pin the package versions. R default is to install the latest version.
    for (auto* pkg : rpkg_database_.get_dependencies(packages_)) {
        if (pkg->is_base) {
            continue;
        }

        // https://stat.ethz.ch/pipermail/r-devel/2018-October/076989.html
        // https://stackoverflow.com/questions/17082341/installing-older-version-of-r-package
        script << "install_version('" << pkg->name << "', '" << pkg->version
               << "', upgrade = 'never', dependencies = FALSE, Ncpus = cores"
               << ")" << std::endl;
    }

    builder.copy({script_}, "/");
    builder.run({STR("Rscript /" << script_.filename()),
                 STR("rm -f /" << script_.filename())});
}

inline void
CRANPackagesManifest::load_from_files(std::vector<FileInfo>& files) {
    SymlinkResolver symlink_resolved{};

    auto resolved = [&](FileInfo const& info) {
        auto path = info.path;
        for (auto& p : symlink_resolved.resolve_symlinks(path)) {
            if (auto* pkg = rpkg_database_.lookup_by_path(p); pkg) {

                LOG_DEBUG(log_) << "resolved: " << path << " to: " << pkg->name;

                auto it = packages_.insert(pkg);
                files_.insert_or_assign(p, *it.first);

                return true;
            }
        }
        return false;
    };

    std::erase_if(files, resolved);
    LOG_INFO(log_) << "Resolved " << files_.size() << " files to "
                   << packages_.size() << " R packages";
};
inline void CRANPackagesManifest::write_to_manifest(
    ManifestFormat::Section& section) const {
    if (packages_.empty()) {
        section.preamble = "# No CRAN packages will be installed";
        return;
    }

    section.preamble =
        STR("# The following " << packages_.size()
                               << " CRAN packages will be installed:");
    std::ostringstream content;
    for (auto const& pkg : packages_) {
        content << "- " << pkg->name << " " << pkg->version << "\n";
    }

    section.content = content.str();
}

// FIXME: rename to DefaultFilesManifest and move the other logic to
// CopyFileManifest
class IgnoreFilesManifest : public ManifestPart {
  public:
    void load_from_files(std::vector<FileInfo>& files) override {
        static FileSystemTrie<ImageFileInfo> const kDefaultImageFiles =
            load_default_files();

        std::erase_if(files, [&](FileInfo const& info) {
            auto& path = info.path;
            if (*kIgnoredFiles.find_last_matching(path)) {
                LOG_DEBUG(log_) << "resolving: " << path << " to: ignored";
                return true;
            }
            return false;
        });

        SymlinkResolver resolver;
        std::erase_if(files, [&](FileInfo const& info) {
            auto& path = info.path;
            for (auto& p : resolver.resolve_symlinks(path)) {
                if (auto f = kDefaultImageFiles.find(p); f) {
                    // TODO: check the size, perm, ...
                    LOG_DEBUG(log_) << "resolving: " << path
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
            auto& path = info.path;
            for (auto const& d : fontconfig_dirs) {
                if (is_sub_path(path, d)) {
                    if (path.filename() == ".uuid") {
                        LOG_DEBUG(log_)
                            << "resolving: " << path << " to: ignored";
                        return true;
                    }
                }
            }
            return false;
        });
    }

    void write_to_manifest(ManifestFormat::Section&) const override {}

  private:
    static FileSystemTrie<ImageFileInfo> load_default_files() {
        auto default_files = []() {
            if (fs::exists(kImageFileCache)) {
                return DefaultImageFiles::from_file(kImageFileCache);
            } else {
                LOG_INFO(log_)
                    << "Default image file cache " << kImageFileCache
                    << " does not exists, creating from image " << kImageName;

                auto files = DefaultImageFiles::from_image(kImageName,
                                                           kBlacklistPatterns);
                try {
                    fs::create_directories(kImageFileCache.parent_path());
                    std::ofstream out{kImageFileCache};
                    files.save(out);
                } catch (std::exception const& e) {
                    LOG_WARN(log_)
                        << "Unable to store default image file list to "
                        << kImageFileCache << ": " << e.what();
                }
                return files;
            }
        }();

        LOG_DEBUG(log_) << "Loaded " << default_files.size()
                        << " default files";

        FileSystemTrie<ImageFileInfo> trie;
        for (auto& info : default_files.files()) {
            trie.insert(info.path, info);
        }
        return trie;
    }

    static inline Logger& log_ = LogManager::logger("manifest.ignore");

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

class CopyFileManifest : public ManifestPart {
  public:
    CopyFileManifest(fs::path const& cwd, fs::path const& output_dir,
                     AbsolutePathSet& results)
        : cwd_{cwd}, archive_{output_dir / "archive.tar"}, results_{results} {}

    void load_from_files(std::vector<FileInfo>& files) override;
    void load_from_manifest(std::istream& stream) override;
    void write_to_manifest(ManifestFormat::Section& section) const override;
    void write_to_docker(DockerFileBuilder& builder) const override;

    enum class Status {
        Copy,
        Ignore,
        IgnoreDidNotExistBefore,
        IgnoreNoLongerExist,
        IgnoreNotAccessible,
        IgnoreCWD
    };

  private:
    fs::path const& cwd_;
    fs::path archive_;
    std::map<fs::path, Status> files_;
    AbsolutePathSet& results_;
    static inline Logger& log_ = LogManager::logger("manifest.copy");
};

namespace std {
inline std::ostream& operator<<(std::ostream& os,
                                CopyFileManifest::Status status) {
    switch (status) {
    case CopyFileManifest::Status::Copy:
        os << "Copy";
        break;
    case CopyFileManifest::Status::Ignore:
        os << "Ignore";
        break;
    case CopyFileManifest::Status::IgnoreDidNotExistBefore:
        os << "Ignore, did not exist before";
        break;
    case CopyFileManifest::Status::IgnoreNoLongerExist:
        os << "Ignore, no longer exists";
        break;
    case CopyFileManifest::Status::IgnoreNotAccessible:
        os << "Ignore, not accessible";
        break;
    case CopyFileManifest::Status::IgnoreCWD:
        os << "Ignore, it is the current working directory";
        break;
    }
    return os;
}

}; // namespace std

inline void CopyFileManifest::load_from_manifest(std::istream& stream) {
    files_.clear();

    std::string line;
    while (std::getline(stream, line)) {
        line = string_trim(line);
        bool copy;

        if (line.starts_with("C")) {
            copy = true;
        } else if (line.starts_with("R")) {
            copy = false;
        } else {
            LOG_WARN(log_) << "Invalid line: " << line;
            continue;
        }

        line = line.substr(1);
        line = string_trim(line);
        if (line.starts_with('"')) {
            if (line.ends_with('"')) {
                line = line.substr(1, line.size() - 2);
            } else {
                LOG_WARN(log_) << "Invalid path: " << line;
                continue;
            }
        }

        if (copy) {
            files_.emplace(line, Status::Copy);
        } else {
            results_.insert(line);
        }
    }

    LOG_INFO(log_) << "Loaded " << files_.size() << " files from manifest";
}

inline void
CopyFileManifest::write_to_manifest(ManifestFormat::Section& section) const {
    if (files_.empty()) {
        section.preamble = "No files will be copies";
        return;
    }

    section.preamble =
        STR("The following "
            << files_.size() << " files has not been resolved.\n"
            << "By default, they will be copied, unless explicitly ignored.\n"
            << "C - mark file to be copied into the image.\n"
            << "R - mark as additional result file.");

    std::ostringstream content;
    for (auto& [path, status] : files_) {
        if (status == Status::Copy) {
            content << "C " << path << "\n";
        } else {
            content << ManifestFormat::comment() << " " << path << " "
                    << ManifestFormat::comment() << " " << status << "\n";
        }
    }
    section.content = content.str();
}

inline void CopyFileManifest::load_from_files(std::vector<FileInfo>& files) {
    for (auto& f : files) {
        auto& path = f.path;
        Status status;

        if (!f.existed_before) {
            status = Status::IgnoreDidNotExistBefore;
        } else if (fs::equivalent(path, cwd_)) {
            status = Status::IgnoreCWD;
        } else {
            switch (check_accessibility(path)) {
            case AccessStatus::Accessible:
                status = Status::Copy;
                break;
            case AccessStatus::DoesNotExist:
                status = Status::IgnoreNoLongerExist;
                break;
            case AccessStatus::InsufficientPermission:
                status = Status::IgnoreNotAccessible;
                break;
            }
        }

        LOG_DEBUG(log_) << "resolved: " << path << " to: " << status;

        // TODO: check size / sha1

        files_.emplace(path, status);
    }
}

inline void
CopyFileManifest::write_to_docker(DockerFileBuilder& builder) const {
    std::vector<fs::path> copy_files;
    for (auto& [path, status] : files_) {
        if (status == Status::Copy) {
            copy_files.push_back(path);
            if (fs::is_symlink(path)) {
                copy_files.push_back(fs::read_symlink(path));
            }
        }
    }

    std::sort(copy_files.begin(), copy_files.end());

    if (copy_files.empty()) {
        return;
    }
    create_tar_archive(archive_, copy_files);

    builder.copy({archive_}, archive_);

    builder.run({STR("tar -x --file " << archive_ << " --absolute-names"),
                 STR("rm -f " << archive_)});
}

class Manifest {
  public:
    template <std::derived_from<ManifestPart> T, typename... Args>
    void add(std::string const& key, Args&&... args) {
        parts_.emplace(key, std::make_unique<T>(std::forward<Args>(args)...));
        index_.emplace_back(key);
    }

    void load_from_files(std::vector<FileInfo>& files) {
        for (auto& name : index_) {
            parts_.at(name)->load_from_files(files);
        }
    }

    void load_from_manifest(std::istream& stream) {
        auto format = ManifestFormat::from_stream(stream);
        for (auto& [name, content, _] : format) {
            auto it = parts_.find(name);
            if (it == parts_.end()) {
                throw std::runtime_error("Unknown section: " + name);
            }
            auto& m = *(it->second);
            std::istringstream section_stream{content};
            m.load_from_manifest(section_stream);
        }
    };

    void write_to_manifest(ManifestFormat& format) const {
        for (auto& name : index_) {
            ManifestFormat::Section section{name};
            parts_.at(name)->write_to_manifest(section);
            if (!section.content.empty()) {
                format.add_section(section);
            }
        }
    };

    void write_to_docker(DockerFileBuilder& builder) const {
        for (auto& name : index_) {
            builder.nl();
            parts_.at(name)->write_to_docker(builder);
        }
    };

  private:
    static inline Logger& log_ = LogManager::logger("composed-manifest");
    std::unordered_map<std::string, std::unique_ptr<ManifestPart>> parts_;
    std::vector<std::string> index_;
};

#endif // MANIFEST_H
