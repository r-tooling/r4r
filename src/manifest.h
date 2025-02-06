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

enum class FileStatus {
    Copy,
    Ignore,
    IgnoreDidNotExistBefore,
    IgnoreNoLongerExist,
    IgnoreNotAccessible,
    IgnoreCWD
};

namespace std {
inline std::ostream& operator<<(std::ostream& os, FileStatus status) {
    switch (status) {
    case FileStatus::Copy:
        os << "Copy";
        break;
    case FileStatus::Ignore:
        os << "Ignore";
        break;
    case FileStatus::IgnoreDidNotExistBefore:
        os << "Ignore, did not exist before";
        break;
    case FileStatus::IgnoreNoLongerExist:
        os << "Ignore, no longer exists";
        break;
    case FileStatus::IgnoreNotAccessible:
        os << "Ignore, not accessible";
        break;
    case FileStatus::IgnoreCWD:
        os << "Ignore, it is the current working directory";
        break;
    }
    return os;
}

}; // namespace std

// TODO: split this class to a smaller ones.
class Manifest {
  public:
    using Files = std::unordered_map<fs::path, FileStatus>;

    explicit Manifest(fs::path const& output_dir)
        : archive_{output_dir / "archive.tar"},
          cran_install_script_{output_dir / "install_r_packages.R"} {}

    void write_to_docker(DockerFileBuilder& builder) const;
    void add_deb_package(DebPackage const& pkg);
    void add_cran_package(RPackage const& pkg);
    void add_file(fs::path const& path, FileStatus status);
    Files& files() { return files_; }

  private:
    void write_deb_packages(DockerFileBuilder& builder) const;
    void write_cran_packages(DockerFileBuilder& builder) const;
    void write_files(DockerFileBuilder& builder) const;

    fs::path archive_;
    fs::path cran_install_script_;
    Files files_;

    // TODO: use pointers to databases
    std::vector<RPackage> cran_packages_;
    std::vector<DebPackage> deb_packages_;
};

void Manifest::add_cran_package(RPackage const& pkg) {
    cran_packages_.push_back(pkg);
}

void Manifest::add_deb_package(DebPackage const& pkg) {
    deb_packages_.push_back(pkg);
}

void Manifest::add_file(fs::path const& path, FileStatus status) {
    files_.emplace(path, status);
}

void Manifest::write_to_docker(DockerFileBuilder& builder) const {
    write_deb_packages(builder);
    write_cran_packages(builder);
    write_files(builder);
}

void Manifest::write_deb_packages(DockerFileBuilder& builder) const {

    // TODO: the main problem with this implementation is that
    // it ignores the fact a package can come from multiple repos
    // and that these repos need to be installed.

    if (deb_packages_.empty()) {
        return;
    }

    std::vector<std::string> pkgs;
    std::unordered_set<std::string> seen;
    pkgs.reserve(deb_packages_.size());

    for (auto const& pkg : deb_packages_) {
        std::string p = pkg.name + "=" + pkg.version;
        if (seen.insert(p).second) {
            pkgs.push_back(p);
        }
    }

    std::sort(pkgs.begin(), pkgs.end());

    std::string line = string_join(pkgs, " \\\n      ");

    builder.run({
        "apt-get update -y",
        "apt-get install -y --no-install-recommends " + line,
    });
}

inline void Manifest::write_files(DockerFileBuilder& builder) const {
    std::vector<fs::path> copy_files;
    for (auto& [path, status] : files_) {
        if (status != FileStatus::Copy) {
            continue;
        }

        copy_files.push_back(path);
        if (fs::is_symlink(path)) {
            copy_files.push_back(fs::read_symlink(path));
        }
    }

    if (copy_files.empty()) {
        return;
    }

    std::sort(copy_files.begin(), copy_files.end());

    create_tar_archive(archive_, copy_files);

    builder.copy({archive_}, archive_);
    builder.run({STR("tar -x --file " << archive_ << " --absolute-names"),
                 STR("rm -f " << archive_)});
}

inline void Manifest::write_cran_packages(DockerFileBuilder& builder) const {
    if (cran_packages_.empty()) {
        return;
    }

    std::ofstream script(cran_install_script_);
    // TODO: parameterize the max cores
    script << "cores <- min(parallel::detectCores(), 4)\n"
           << "tmp_dir <- tempdir()\n"
           << "install.packages('remotes', lib = c(tmp_dir))\n"
           << "require('remotes', lib.loc = c(tmp_dir))\n"
           << "on.exit(unlink(tmp_dir, recursive = TRUE))\n"
           << "\n"
           << "# installing packages\n\n";

    std::unordered_set<std::string> seen;
    // we have to install the dependencies ourselves otherwise we cannot get
    // pin the package versions. R default is to install the latest version.
    for (auto& pkg : cran_packages_) {
        // https://stat.ethz.ch/pipermail/r-devel/2018-October/076989.html
        // https://stackoverflow.com/questions/17082341/installing-older-version-of-r-package

        if (!seen.insert(pkg.name).second) {
            continue;
        }

        script << "install_version('" << pkg.name << "', '" << pkg.version
               << "', upgrade = 'never', dependencies = FALSE, Ncpus = cores"
               << ")" << std::endl;
    }

    builder.copy({cran_install_script_}, "/");
    builder.run({STR("Rscript /" << cran_install_script_.filename()),
                 STR("rm -f /" << cran_install_script_.filename())});
}

class ManifestFormat {
  public:
    static constexpr char comment() noexcept { return kCommentChar; }

    struct Section {
        std::string name;
        std::string content;
        std::string preamble;
    };

    ManifestFormat() = default;

    using iterator = std::vector<Section>::iterator;
    using const_iterator = std::vector<Section>::const_iterator;

    [[nodiscard]] iterator begin() { return sections_.begin(); }
    [[nodiscard]] iterator end() { return sections_.end(); }
    [[nodiscard]] const_iterator begin() const { return sections_.begin(); }
    [[nodiscard]] const_iterator end() const { return sections_.end(); }
    [[nodiscard]] const_iterator cbegin() const { return sections_.cbegin(); }
    [[nodiscard]] const_iterator cend() const { return sections_.cend(); }

    void preamble(std::string preamble) { preamble_ = std::move(preamble); }

    Section* get_section(std::string const& name) {
        auto it =
            std::find_if(sections_.begin(), sections_.end(),
                         [&](Section const& x) { return x.name == name; });
        if (it == sections_.end()) {
            return nullptr;
        }
        return &*it;
    }

    Section& add_section(Section section) {
        if (!is_valid_section_name(section.name)) {
            throw std::invalid_argument("Invalid section name: " +
                                        section.name);
        }

        if (get_section(section.name) != nullptr) {
            throw std::runtime_error(
                STR("section: " << section.name << " already exists"));
        }

        return sections_.emplace_back(section);
    }

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
                section = &manifest.add_section({name, "", ""});
                continue;
            }
            if (section == nullptr) {
                throw std::runtime_error(
                    "Content line encountered before any section header: " +
                    line);
            }
            if (!section->content.empty()) {
                section->content.push_back('\n');
            }

            if (line.starts_with(kCommentChar)) {
                continue;
            }

            auto pos = line.find(kCommentChar);
            if (pos != std::string::npos) {
                line = line.substr(0, pos);
                line = string_trim(line);
            }

            section->content.append(line);
        }

        return manifest;
    }

    void write(std::ostream& out) const {
        // TODO: use kCommentChar
        if (!preamble_.empty()) {
            prefixed_ostream(out, "# ", [&] { out << preamble_; });
            out << "\n\n";
        }

        for (auto const& [name, content, preamble] : sections_) {
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

// inline void CopyFileManifest::load_from_manifest(std::istream& stream) {
//     files_.clear();
//
//     std::string line;
//     while (std::getline(stream, line)) {
//         line = string_trim(line);
//         bool copy;
//
//         if (line.starts_with("C")) {
//             copy = true;
//         } else if (line.starts_with("R")) {
//             copy = false;
//         } else {
//             LOG_WARN(log_) << "Invalid line: " << line;
//             continue;
//         }
//
//         line = line.substr(1);
//         line = string_trim(line);
//         if (line.starts_with('"')) {
//             if (line.ends_with('"')) {
//                 line = line.substr(1, line.size() - 2);
//             } else {
//                 LOG_WARN(log_) << "Invalid path: " << line;
//                 continue;
//             }
//         }
//
//         if (copy) {
//             files_.emplace(line, Status::Copy);
//         } else {
//             results_.insert(line);
//         }
//     }
//
//     LOG_INFO(log_) << "Loaded " << files_.size() << " files from manifest";
// }
//
// inline void
// CopyFileManifest::write_to_manifest(ManifestFormat::Section& section) const {
//     if (files_.empty()) {
//         section.preamble = "No files will be copies";
//         return;
//     }
//
//     section.preamble =
//         STR("The following "
//             << files_.size() << " files has not been resolved.\n"
//             << "By default, they will be copied, unless explicitly
//             ignored.\n"
//             << "C - mark file to be copied into the image.\n"
//             << "R - mark as additional result file.");
//
//     std::ostringstream content;
//     for (auto& [path, status] : files_) {
//         if (status == Status::Copy) {
//             content << "C " << path << "\n";
//         } else {
//             content << ManifestFormat::comment() << " " << path << " "
//                     << ManifestFormat::comment() << " " << status << "\n";
//         }
//     }
//     section.content = content.str();
// }

// class Manifest {
//   public:
//     template <std::derived_from<ManifestPart> T, typename... Args>
//     void add(std::string const& key, Args&&... args) {
//         parts_.emplace(key,
//         std::make_unique<T>(std::forward<Args>(args)...));
//         index_.emplace_back(key);
//     }
//
//     void load_from_files(std::vector<FileInfo>& files) {
//         for (auto& name : index_) {
//             parts_.at(name)->load_from_files(files);
//         }
//     }
//
//     void load_from_manifest(std::istream& stream) {
//         auto format = ManifestFormat::from_stream(stream);
//         for (auto& [name, content, _] : format) {
//             auto it = parts_.find(name);
//             if (it == parts_.end()) {
//                 throw std::runtime_error("Unknown section: " + name);
//             }
//             auto& m = *(it->second);
//             std::istringstream section_stream{content};
//             m.load_from_manifest(section_stream);
//         }
//     };
//
//     void write_to_manifest(ManifestFormat& format) const {
//         for (auto& name : index_) {
//             ManifestFormat::Section section{name};
//             parts_.at(name)->write_to_manifest(section);
//             if (!section.content.empty()) {
//                 format.add_section(section);
//             }
//         }
//     };
//
//     void write_to_docker(DockerFileBuilder& builder) const {
//         for (auto& name : index_) {
//             builder.nl();
//             parts_.at(name)->write_to_docker(builder);
//         }
//     };
//
//   private:
//     static inline Logger& log_ = LogManager::logger("composed-manifest");
//     std::unordered_map<std::string, std::unique_ptr<ManifestPart>> parts_;
//     std::vector<std::string> index_;
// };

#endif // MANIFEST_H
