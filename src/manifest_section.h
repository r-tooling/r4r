#ifndef MANIFEST_SECTION_H
#define MANIFEST_SECTION_H

#include "manifest.h"
#include "manifest_format.h"
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
        stream << "The following "
               << " files has not been resolved.\n"
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

#endif // MANIFEST_SECTION_H
