#ifndef MANIFEST_H
#define MANIFEST_H

#include "dpkg_database.h"
#include "ignore_file_map.h"
#include "rpkg_database.h"
#include "user.h"
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <unordered_set>
#include <vector>

enum class FileStatus {
    Copy,
    Result,
    IgnoreDidNotExistBefore,
    IgnoreNoLongerExist,
    IgnoreNotAccessible,
    IgnoreDirectory
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
    case FileStatus::IgnoreDirectory:
        os << "Ignore, it is a directory";
        break;
    }
    return os;
}

}; // namespace std

struct Manifest {
    using Files = std::unordered_map<fs::path, FileStatus>;

    std::vector<std::string> cmd;
    fs::path cwd;
    std::unordered_map<std::string, std::string> envir;
    UserInfo user;
    std::string timezone;
    std::string distribution;
    std::string distribution_version;
    std::string base_image;
    fs::path default_image_files_cache;
    IgnoreFileMap ignore_file_map;

    Files copy_files;
    std::unordered_set<fs::path> symlinks;
    std::unordered_set<RPackage const*> r_packages;
    std::unordered_set<DebPackage const*> deb_packages;
};

#endif // MANIFEST_H
