#ifndef FS_H
#define FS_H

#include "util.h"
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

class SymlinkResolver {
  public:
    SymlinkResolver(std::unordered_map<fs::path, fs::path> root_symlinks)
        : root_symlinks_{std::move(root_symlinks)} {}

    SymlinkResolver() { populate_root_symlinks(root_symlinks_); }

    std::vector<fs::path> get_root_symlink(fs::path const& path) {
        std::vector<fs::path> result = {path};

        // checks wither path is a subpath of b and if it is, tries to see if
        // the same file could be find by using a instead of b
        auto test = [&](fs::path const& a, fs::path const& b) {
            if (is_sub_path(path, b)) {
                fs::path candidate = a / path.lexically_relative(b);

                std::error_code ec;
                if (fs::exists(candidate, ec) &&
                    fs::equivalent(candidate, path, ec)) {
                    result.push_back(candidate);
                    return true;
                }
            }
            return false;
        };

        for (auto& [symlink, target] : root_symlinks_) {
            if (test(symlink, target)) {
                break;
            }
            if (test(target, symlink)) {
                break;
            }
        }

        return result;
    }

  private:
    static void
    populate_root_symlinks(std::unordered_map<fs::path, fs::path>& symlinks) {
        fs::path root = "/";
        for (auto& entry : fs::directory_iterator(root)) {
            if (entry.is_symlink()) {
                std::error_code ec;
                fs::path target = fs::read_symlink(entry.path(), ec);
                if (!target.is_absolute()) {
                    target = fs::canonical(root / target);
                }
                if (!ec && fs::is_directory(target)) {
                    symlinks[entry.path()] = target;
                }
            }
        }
    }

    // a map of the root symlinks:
    // - /lib => /usr/lib
    // - /bin => /usr/bin
    // ...
    std::unordered_map<fs::path, fs::path> root_symlinks_;
};

class TempFile {
  public:
    TempFile(std::string const& prefix, std::string const& suffix,
             bool delete_on_destruction = true)
        : delete_on_destruction_(delete_on_destruction) {

        path_ = create_temp_file(prefix, suffix);
    }

    ~TempFile() {
        if (delete_on_destruction_ && fs::exists(path_)) {
            std::error_code ec;
            fs::remove(path_, ec);
        }
    }

    TempFile(TempFile const&) = delete;
    TempFile& operator=(TempFile const&) = delete;
    TempFile(TempFile&& other) = delete;
    TempFile& operator=(TempFile&& other) = delete;

    fs::path const& operator*() const noexcept { return path_; }
    fs::path const* operator->() const noexcept { return &path_; }
    fs::path const& path() const noexcept { return path_; }

    static fs::path create_temp_file(std::string const& prefix,
                                     std::string const& suffix);

  private:
    fs::path path_;
    bool delete_on_destruction_;
};

inline fs::path TempFile::create_temp_file(std::string const& prefix,
                                           std::string const& suffix) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;

    fs::path temp_dir = fs::temp_directory_path();
    int attempts = 42;

    while (attempts-- > 0) {
        fs::path temp_file = temp_dir / STR(prefix << dist(gen) << suffix);
        if (!fs::exists(temp_file)) {
            return temp_file;
        }
    }

    throw make_system_error(
        errno, STR("Failed to create a unique temporary file in " << temp_dir));
}
#endif // FS_H
