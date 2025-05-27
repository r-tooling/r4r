#ifndef UTIL_FS_H
#define UTIL_FS_H

#include "common.h"
#include <filesystem>
#include <fstream>
#include <optional>
#include <queue>
#include <random>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

inline bool is_sub_path(fs::path const& path, fs::path const& base) {
    auto const mismatch =
        std::mismatch(path.begin(), path.end(), base.begin(), base.end());
    return mismatch.second == base.end();
}

class SymlinkResolver {
  public:
    explicit SymlinkResolver(fs::path const& path = "/")
        : symlinks_{populate_symlinks(path)} {}

    std::unordered_set<fs::path> resolve_symlinks(fs::path const& path) const {
        // TODO: check the error code and generate warnings
        std::error_code ec;
        std::unordered_set<fs::path> result;
        std::queue<fs::path> wl;

        // checks whether `p` is a sub path of `b` and if it is, tries to see
        // if the same file could be found using `a` instead of `b`
        // if the result is a symlink, it additionally pushed the link as well
        auto test = [&](fs::path const& p, fs::path const& a,
                        fs::path const& b) {
            if (is_sub_path(p, b)) {
                fs::path candidate = a / p.lexically_relative(b);

                if (fs::exists(candidate, ec) &&
                    fs::equivalent(candidate, p, ec)) {
                    wl.push(candidate);
                    return true;
                }
            }
            return false;
        };

        wl.push(path);

        while (!wl.empty()) {
            auto p = wl.front();
            wl.pop();

            if (result.contains(p)) {
                continue;
            }

            result.insert(p);

            for (auto& [symlink, target] : symlinks_) {
                if (test(p, symlink, target)) {
                    break;
                }
                if (test(p, target, symlink)) {
                    break;
                }
            }

            if (fs::is_symlink(p)) {
                wl.push(fs::read_symlink(p));
            }
        }

        return result;
    }

  private:
    static std::unordered_map<fs::path, fs::path>
    populate_symlinks(fs::path const& root) {
        std::unordered_map<fs::path, fs::path> symlinks;

        for (auto const& entry : fs::directory_iterator(root)) {
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
        return symlinks;
    }

    // a map of the root symlinks:
    // - /lib => /usr/lib
    // - /bin => /usr/bin
    // ...
    std::unordered_map<fs::path, fs::path> symlinks_;
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
    [[nodiscard]] fs::path const& path() const noexcept { return path_; }

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

enum class AccessStatus { Accessible, DoesNotExist, InsufficientPermission };

inline AccessStatus check_accessibility(fs::path const& p) {
    try {
        if (!fs::exists(p)) {
            return AccessStatus::DoesNotExist;
        }

        if (fs::is_directory(p)) {
            try {
                fs::directory_iterator it(p);
            } catch (std::exception const&) {
                return AccessStatus::InsufficientPermission;
            }
        } else {
            std::ifstream f(p);
            if (!f.is_open()) {
                return AccessStatus::InsufficientPermission;
            }
        }

        return AccessStatus::Accessible;
    } catch (fs::filesystem_error const&) {
        return AccessStatus::InsufficientPermission;
    }
}

class AbsolutePathSet {
  public:
    bool insert(fs::path const& p) {
        std::error_code ec;
        fs::path resolved = fs::absolute(p, ec);
        if (ec) {
            // FIXME: log some warning?
            // if (ec) {
            //     LOG_WARN(log_) << "Failed to resolve absolute path: " << line
            //                    << " - " << ec.message();
            // }
            return false;
        }

        auto [_, inserted] = paths_.insert(resolved);
        return inserted;
    }

    bool contains(fs::path const& p) const { return paths_.contains(p); }

    size_t size() const { return paths_.size(); }

    bool empty() const { return paths_.empty(); }

    using const_iterator = std::unordered_set<fs::path>::const_iterator;

    const_iterator begin() const noexcept { return paths_.begin(); }
    const_iterator end() const noexcept { return paths_.end(); }

  private:
    std::unordered_set<fs::path> paths_;
};

template <typename T>
inline void write_to_file(fs::path const& path, T const& data) {
    std::ofstream outfile(path, std::ios::trunc);

    if (!outfile) {
        throw make_system_error(
            errno, STR("Failed to open file for writing: " << path));
    }

    if (!(outfile << data)) {
        auto e =
            make_system_error(errno, STR("Failed to write to file: " << path));
        outfile.close();
        throw e;
    }
}

inline std::string read_from_file(fs::path const& path) {
    std::ifstream infile(path);
    if (!infile) {
        throw make_system_error(
            errno, STR("Failed to open file for reading: " << path));
    }

    std::ostringstream buffer;
    buffer << infile.rdbuf();
    if (infile.fail() && !infile.eof()) {
        throw make_system_error(errno,
                                STR("Failed to read from file: " << path));
    }
    return buffer.str();
}

inline std::optional<fs::path> resolve_symlink(fs::path const& path) {
    std::error_code ec;
    fs::path target = fs::read_symlink(path, ec);
    if (ec) {
        return {};
    }

    if (!target.is_absolute()) {
        target = fs::absolute(path.parent_path() / target);
    }

    return target;
}

inline std::string file_type_str(fs::path const& p) {
    fs::file_status st = fs::symlink_status(p);

    switch (st.type()) {
    case fs::file_type::none:
        return "none";
    case fs::file_type::not_found:
        return "not found";
    case fs::file_type::regular:
        return "regular file";
    case fs::file_type::directory:
        return "directory";
    case fs::file_type::symlink:
        return "symlink";
    case fs::file_type::block:
        return "block device";
    case fs::file_type::character:
        return "character device";
    case fs::file_type::fifo:
        return "FIFO/pipe";
    case fs::file_type::socket:
        return "socket";
    case fs::file_type::unknown:
        return "unknown";
    default:
        UNREACHABLE();
    }
}

#endif // UTIL_FS_H
