#ifndef MANIFEST_SECTION_H
#define MANIFEST_SECTION_H

#include "manifest.h"
#include "manifest_format.h"
#include "rpkg_database.h"
#include <string>

class ManifestSection {
  public:
    explicit ManifestSection(std::string name) : name_{std::move(name)} {}

    virtual ~ManifestSection() = default;

    virtual void load(std::istream& stream, Manifest& manifest) = 0;
    virtual bool save(std::ostream& stream, Manifest const& manifest) = 0;
    [[nodiscard]] std::string const& name() const { return name_; }

  private:
    std::string name_;
};

class CopyFilesManifestSection : public ManifestSection {
  public:
    CopyFilesManifestSection() : ManifestSection("copy") {}

    void load(std::istream& stream, Manifest& manifest) override;
    bool save(std::ostream& stream, Manifest const& manifest) override;
};

inline void CopyFilesManifestSection::load(std::istream& stream,
                                           Manifest& manifest) {
    std::string line;

    manifest.copy_files.clear();

    while (std::getline(stream, line)) {
        FileStatus status{};

        if (line.starts_with("C")) {
            status = FileStatus::Copy;
        } else if (line.starts_with("R")) {
            status = FileStatus::Result;
        } else {
            LOG(WARN) << "Invalid manifest line: " << line;
            continue;
        }

        line = line.substr(1);
        line = string_trim(line);
        if (line.starts_with('"')) {
            if (line.ends_with('"')) {
                line = line.substr(1, line.size() - 2);
            } else {
                LOG(WARN) << "Invalid path: " << line;
                continue;
            }
        }

        manifest.copy_files.emplace(line, status);
    }
}

inline bool CopyFilesManifestSection::save(std::ostream& stream,
                                           Manifest const& manifest) {
    if (manifest.copy_files.empty()) {
        return false;
    }

    with_prefixed_ostream(stream, ManifestFormat::prefixed_comment(), [&] {
        stream << "The following files have not been resolved.\n"
               << "# - ignores the file.\n"
               << "C - marks the file to be copied into the image.\n"
               << "R - marks the file as a result file.\n";
    });

    std::vector<std::pair<fs::path, FileStatus>> sorted_files;
    sorted_files.reserve(manifest.copy_files.size());
    for (auto const& f : manifest.copy_files) {
        sorted_files.emplace_back(f);
    }

    std::sort(
        sorted_files.begin(), sorted_files.end(),
        [](auto const& lhs, auto const& rhs) { return lhs.first < rhs.first; });

    for (auto const& [path, status] : sorted_files) {
        switch (status) {
        case FileStatus::Copy:
            stream << "C " << path.string() << "\n";
            break;
        case FileStatus::Result:
            stream << "R " << path.string() << "\n";
            break;
        case FileStatus::IgnoreNoLongerExist:
            // nothing we can do
            break;
        default:
            stream << ManifestFormat::comment() << ' ' << path.string() << ' '
                   << ManifestFormat::comment() << ' ' << status << '\n';
        }
    }

    return true;
}

class RPackagesManifestSection : public ManifestSection {
  public:
    RPackagesManifestSection() : ManifestSection("r-packages") {}

    void load(std::istream& stream, Manifest& manifest) override;
    bool save(std::ostream& stream, Manifest const& manifest) override;
};

inline void
RPackagesManifestSection::load([[maybe_unused]] std::istream& stream,
                               [[maybe_unused]] Manifest& manifest) {
    std::string line;

    // TODO: just filter out the packages we want to ignore here
    // Otherwise, we would need to save more information in the
    // Manifest to be ableto reconstruct a RPackage

    /*
    while (std::getline(stream, line)) {
        if (line.empty() || line.starts_with(ManifestFormat::comment())) {
            continue;
        }

        PackageStatus status{};

        if (line.starts_with("I")) {
            status = PackageStatus::Install;
        } else {
            LOG(WARN) << "Invalid manifest line: " << line;
            continue;
        }

        auto parts = string_split(line, ' '); // TODO: make more robust to
    various whitespaces
        //manifest.r_packages.insert(pkg);
        if (parts.size() < 3) {
            LOG(WARN) << "Invalid R package line: " << line;
            continue;
        }
        std::string repo = string_tolowercase(string_trim(parts[1]));
        std::string name = string_trim(parts[2]);
        if(name.empty()) {
            LOG(WARN) << "Invalid R package name or origin: " << line;
            continue;
        }
        if (repo == "cran") {
            manifest.r_packages.insert(RPackage::CRAN);
        } else if (repo == "github") {
            // parse the name. It is as follows: org/name@ref
            auto end_org = name.find('/');
            if (end_org == std::string::npos) {
                LOG(WARN) << "Invalid or missing GitHub repository organisation:
    " << name; continue;
            }
            std::string org = name.substr(0, end_org);
            std::string rest = name.substr(end_org);
            auto end_name = rest.find('@');
            std::string repo_name;
            std::string ref;
            if (end_name == std::string::npos) {
                repo_name = rest;
                ref = "HEAD"; // default to HEAD if no ref is provided
            } else {
                repo_name = rest.substr(0, end_name);
                ref = rest.substr(end_name + 1);
            }
            if (org.empty() || repo_name.empty()) {
                LOG(WARN) << "Invalid GitHub repository org or name: " << name;
                continue;
            }
            auto gh = RPackage::GitHub{org, repo_name, ref};
            manifest.r_packages.insert(gh);
        } else {
            LOG(WARN) << "Unknown R package repository type: " << repo;
        }
    }
    */
}

inline bool RPackagesManifestSection::save(std::ostream& stream,
                                           Manifest const& manifest) {
    if (manifest.r_packages.empty()) {
        return false;
    }

    with_prefixed_ostream(stream, ManifestFormat::prefixed_comment(), [&] {
        stream << "The following R packages have been resolved.\n"
               << "# - ignores the package.\n"
               << "cran packageName version - marks the package from CRAN at "
                  "version"
                  "to be installed in the image.\n"
               << "github org/name@ref - marks the package from GitHub to be "
                  "installed in the image.\n";
    });

    std::vector<RPackage const*> sorted_packages;
    sorted_packages.reserve(manifest.r_packages.size());
    for (auto const& p : manifest.r_packages) {
        sorted_packages.emplace_back(p);
    }

    // TODO: rather add a operator< for RPackage (using std::tie)
    std::sort(
        sorted_packages.begin(), sorted_packages.end(),
        [](RPackage const* lhs, RPackage const* rhs) {
            std::string left;
            std::string right;
            if (std::holds_alternative<RPackage::GitHub>(lhs->repository)) {
                left = std::get<RPackage::GitHub>(lhs->repository).org + '/' +
                       std::get<RPackage::GitHub>(lhs->repository).name + '@' +
                       std::get<RPackage::GitHub>(lhs->repository).ref;
            } else if (std::holds_alternative<RPackage::CRAN>(
                           lhs->repository)) {
                left = lhs->name;
            } else {
                LOG(WARN) << "Unknown R package repository type for package "
                          << lhs->name;
                return false; // do not sort unknown types
            }

            if (std::holds_alternative<RPackage::GitHub>(rhs->repository)) {
                right = std::get<RPackage::GitHub>(rhs->repository).org + '/' +
                        std::get<RPackage::GitHub>(rhs->repository).name + '@' +
                        std::get<RPackage::GitHub>(rhs->repository).ref;
            } else if (std::holds_alternative<RPackage::CRAN>(
                           rhs->repository)) {
                right = rhs->name;
            } else {
                LOG(WARN) << "Unknown R package repository type for package "
                          << rhs->name;
                return false; // do not sort unknown types
            }

            return left < right;
        });

    for (auto const* pkg : manifest.r_packages) {
        if (std::holds_alternative<RPackage::GitHub>(pkg->repository)) {
            auto const& gh = std::get<RPackage::GitHub>(pkg->repository);
            stream << "github " << gh.org << '/' << gh.name << '@' << gh.ref
                   << '\n';
        } else if (std::holds_alternative<RPackage::CRAN>(pkg->repository)) {
            stream << "cran " << pkg->name << " " << pkg->version << '\n';
        } else {
            LOG(WARN) << "Unknown R package repository type for package "
                      << pkg->name << " version" << pkg->version;
        }
    }

    return true;
}

#endif // MANIFEST_SECTION_H
