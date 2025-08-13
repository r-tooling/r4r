#ifndef MANIFEST_H
#define MANIFEST_H

#include "dpkg_database.h"
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

enum class PackageStatus { Ignore, Install };

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

inline std::ostream& operator<<(std::ostream& os, PackageStatus status) {
    switch (status) {
    case PackageStatus::Ignore:
        os << "Ignore";
        break;
    case PackageStatus::Install:
        os << "Install";
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

    Files copy_files;
    std::unordered_set<fs::path> symlinks;
    std::unordered_set<RPackage const*> r_packages;
    std::unordered_set<DebPackage const*> deb_packages;
};

#endif // MANIFEST_H
