#ifndef MANIFEST_H
#define MANIFEST_H

#include "archive.h"
#include "dockerfile.h"
#include "dpkg_database.h"
#include "rpkg_database.h"
#include "util_io.h"
#include <algorithm>
#include <bits/types/struct_sched_param.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <vector>

enum class FileStatus {
    Copy,
    Result,
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
    case FileStatus::Result:
        os << "Result file";
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
          permission_script_{output_dir / "permissions.sh"},
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
    static void generate_permissions_script(std::vector<fs::path> const& files,
                                            std::ostream& out);

    fs::path archive_;
    fs::path permission_script_;
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
    for (auto const& [path, status] : files_) {
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
    // FIXME: the paths are not good, it will copy into out/archive.tar
    builder.run({STR("tar -x -f "
                     << archive_
                     << " --same-owner --same-permissions --absolute-names"),
                 STR("rm -f " << archive_)});

    {
        std::ofstream permissions{permission_script_};
        generate_permissions_script(copy_files, permissions);
    }
    builder.copy({permission_script_}, permission_script_);
    // FIXME: the paths are not good, it will copy into out/...sh
    builder.run({STR("bash " << permission_script_),
                 STR("rm -f " << permission_script_)});
}

inline void
Manifest::generate_permissions_script(std::vector<fs::path> const& files,
                                      std::ostream& out) {
    std::set<fs::path> directories;

    for (auto const& file : files) {
        fs::path current = file.parent_path();
        while (!current.empty() && current != "/") {
            directories.insert(current);
            current = current.parent_path();
        }
    }

    std::vector<fs::path> sorted_dirs(directories.begin(), directories.end());

    out << "#!/bin/bash\n\n";
    out << "set -e\n\n";

    for (auto const& dir : sorted_dirs) {
        struct stat info{};
        if (stat(dir.c_str(), &info) != 0) {
            LOG(WARN) << "Warning: Unable to access " << dir << '\n';
            continue;
        }

        uid_t owner = info.st_uid;
        gid_t group = info.st_gid;
        mode_t permissions = info.st_mode & 0777;

        out << "chown " << owner << ":" << group << " " << dir << '\n';
        out << "chmod " << std::oct << permissions << std::dec << " " << dir
            << '\n';
    }
}

inline void Manifest::write_cran_packages(DockerFileBuilder& builder) const {
    if (cran_packages_.empty()) {
        return;
    }

    std::ofstream script(cran_install_script_);
    // TODO: parameterize the max cores
    script << "options(Ncpus=min(parallel::detectCores(), 4))\n\n"
           << "tmp_dir <- tempdir()\n"
           << "install.packages('remotes', lib = c(tmp_dir))\n"
           << "require('remotes', lib.loc = c(tmp_dir))\n"
           << "on.exit(unlink(tmp_dir, recursive = TRUE))\n"
           << "\n\n"
           << "# install wrapper that turns warnings into errors\n"
           // clang-format off
           << "CHK <- function(thunk) {\n"
           << "  withCallingHandlers(force(thunk), warning = function(w) {\n"
           << "if (grepl('installation of package ‘.*’ had non-zero exit status', conditionMessage(w))) { stop(w) }})\n"
           << "}\n\n"
           // clang-format on

           << "# installing packages\n\n";

    std::unordered_set<std::string> seen;
    // we have to install the dependencies ourselves otherwise we cannot get
    // pin the package versions. R default is to install the latest version.
    for (auto const& pkg : cran_packages_) {
        // https://stat.ethz.ch/pipermail/r-devel/2018-October/076989.html
        // https://stackoverflow.com/questions/17082341/installing-older-version-of-r-package

        if (!seen.insert(pkg.name).second) {
            continue;
        }

        script << "CHK(install_version('" << pkg.name << "', '" << pkg.version
               << "', upgrade = 'never', dependencies = FALSE))\n";
    }

    builder.copy({cran_install_script_}, "/");
    // TODO: simplify
    builder.run({STR("Rscript /" << cran_install_script_.filename().string()),
                 STR("rm -f /" << cran_install_script_.filename().string())});
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
            with_prefixed_ostream(out, "# ", [&] { out << preamble_; });
            out << "\n\n";
        }

        for (auto const& [name, content, preamble] : sections_) {
            if (!preamble.empty()) {
                with_prefixed_ostream(out, "# ", [&] { out << preamble; });
                out << '\n';
            }
            out << name << ':' << '\n';
            with_prefixed_ostream(out, "  ", [&] { out << content; });
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
        }
        if ((std::isalpha(name[0]) == 0) && name[0] != '_') {
            return false;
        }
        for (char ch : name) {
            if ((std::isalnum(ch) == 0) && ch != '_') {
                return false;
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

#endif // MANIFEST_H
