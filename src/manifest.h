#ifndef MANIFEST_H
#define MANIFEST_H

#include "dpkg_database.h"
#include "rpkg_database.h"
#include "user.h"
#include <filesystem>
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

struct Manifest {
    using Files = std::unordered_map<fs::path, FileStatus>;

    std::vector<std::string> cmd;
    fs::path cwd;
    std::unordered_map<std::string, std::string> envir;
    UserInfo user;
    std::string timezone;
    std::string distribution;

    Files copy_files;
    std::unordered_set<RPackage const*> r_packages;
    std::unordered_set<DebPackage const*> deb_packages;
};

#endif // MANIFEST_H
