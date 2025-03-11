#ifndef IGNORE_FILE_MAP
#define IGNORE_FILE_MAP

#include "common.h"
#include "filesystem_trie.h"
#include "logger.h"
#include "util_fs.h"
#include <functional>
#include <vector>

class IgnoreFileMap {
  public:
    void add_wildcard(fs::path const& path);
    void add_file(fs::path const& path);
    void add_custom(std::function<bool(fs::path const&)> fun);

    bool ignore(fs::path const& path) const;

  private:
    FileSystemTrie<bool> wildcards_;
    FileSystemTrie<bool> files_;
    std::vector<std::function<bool(fs::path const&)>> custom_;
    SymlinkResolver symlink_resolver;
};

inline void IgnoreFileMap::add_wildcard(fs::path const& path) {
    wildcards_.insert(path, true);
}

inline void IgnoreFileMap::add_file(fs::path const& path) {
    files_.insert(path, true);
}

inline void
IgnoreFileMap::add_custom(std::function<bool(fs::path const&)> fun) {
    custom_.push_back(std::move(fun));
}

inline bool IgnoreFileMap::ignore(fs::path const& path) const {
    if (auto const* it = wildcards_.find_last_matching(path); it && *it) {
        return true;
    }

    for (auto const& p : symlink_resolver.resolve_symlinks(path)) {
        if (auto const* it = files_.find(p); it && *it) {
            return true;
        }
    }

    for (auto const& p : custom_) {
        if (p(path)) {
            return true;
        }
    }

    return false;
}

// ignore the .uuid files from fontconfig
inline bool ignore_font_uuid_files(fs::path const& path) {
    static std::unordered_set<fs::path> const fontconfig_dirs = {
        "/usr/share/fonts", "/usr/share/poppler", "/usr/share/texmf/fonts"};

    for (auto const& d : fontconfig_dirs) {
        if (is_sub_path(path, d)) {
            if (path.filename() == ".uuid") {
                LOG(DEBUG) << "Resolving: " << path << " to: ignored";
                return true;
            }
        }
    }

    return false;
}

#endif // IGNORE_FILE_MAP
