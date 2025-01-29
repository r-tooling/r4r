#ifndef FILE_INFO_H
#define FILE_INFO_H

#include <cstdint>
#include <filesystem>
#include <string>

struct FileInfo {
    std::string user;
    std::string group;
    std::filesystem::perms permissions;
    std::uintmax_t size;
    std::string sha1;
};

#endif // FILE_INFO_H
