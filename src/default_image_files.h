#ifndef DEFAULT_IMAGE_FILES_H
#define DEFAULT_IMAGE_FILES_H

#include "common.h"
#include "logger.h"
#include "process.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

struct ImageFileInfo {
    std::string path;
    std::string user;
    std::string group;
    std::uint32_t permissions;
    std::uintmax_t size;
    std::string sha1;

    bool operator==(ImageFileInfo const& other) const noexcept {
        return std::tie(path, user, group, permissions, size, sha1) ==
               std::tie(other.path, other.user, other.group, other.permissions,
                        other.size, other.sha1);
    }

    bool operator<(ImageFileInfo const& other) const noexcept {
        return std::tie(path, user, group, permissions, size, sha1) <
               std::tie(other.path, other.user, other.group, other.permissions,
                        other.size, other.sha1);
    }
};

namespace std {
template <>
struct hash<ImageFileInfo> {
    std::size_t operator()(ImageFileInfo const& info) const noexcept {
        std::size_t h1 = std::hash<std::string>{}(info.path);
        std::size_t h2 = std::hash<std::string>{}(info.user);
        std::size_t h3 = std::hash<std::string>{}(info.group);
        std::size_t h4 = std::hash<std::uint32_t>{}(info.permissions);
        std::size_t h5 = std::hash<std::uintmax_t>{}(info.size);
        std::size_t h6 = std::hash<std::string>{}(info.sha1);

        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4) ^ (h6 << 5);
    }
};
} // namespace std

class DefaultImageFiles {
  public:
    explicit DefaultImageFiles(std::vector<ImageFileInfo> files)
        : files_{std::move(files)} {}

    static DefaultImageFiles from_file(fs::path const& path);

    static DefaultImageFiles
    from_image(std::string const& image_name,
               std::vector<std::string> const& blacklist_patterns);

    static DefaultImageFiles from_stream(std::istream& stream);

    [[nodiscard]] std::vector<ImageFileInfo> const& files() const {
        return files_;
    }

    [[nodiscard]] size_t size() const { return files_.size(); }

    void save(std::ostream& dst) const;

  private:
    std::vector<ImageFileInfo> files_;
};

inline DefaultImageFiles DefaultImageFiles::from_file(fs::path const& path) {
    LOG(DEBUG) << "Loading default file list from file: " << path;

    std::ifstream file(path);

    if (!file) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }

    return from_stream(file);
}

inline DefaultImageFiles DefaultImageFiles::from_image(
    std::string const& image_name,
    std::vector<std::string> const& blacklist_patterns) {
    LOG(DEBUG) << "Loading default file list from image: " << image_name;

    std::string bf_pattern = string_join(blacklist_patterns, '|');

    auto out =
        Command("docker")
            .arg("run")
            .arg("--rm")
            .arg(image_name)
            .arg("bash")
            .arg("-c")
            // clang-format off
        .arg(STR(
            "DELIM='" NBSP "' " << "BF_PATTERN='" << bf_pattern << "' " <<
            R""(
                    find / \( -type f -or -type l \) 2>/dev/null | grep -vE "$BF_PATTERN" | while IFS= read -r file; do
                        stat="$(stat -c "%U${DELIM}%G${DELIM}%s${DELIM}%a" "$file" 2>/dev/null || echo "error${DELIM}error${DELIM}error${DELIM}error")"
                        sha1="$((sha1sum "$file" 2>/dev/null | cut -d " " -f1) || echo "error")"
                        echo "$file${DELIM}${stat}${DELIM}${sha1}"
                    done
                )""))
            // clang-format on
            .output();

    out.check_success("Unable to initialize default file list for " +
                      image_name);

    std::istringstream stream{out.stdout_data};
    return from_stream(stream);
}

inline DefaultImageFiles DefaultImageFiles::from_stream(std::istream& stream) {
    std::vector<ImageFileInfo> result;
    std::string line;
    while (std::getline(stream, line)) {

        std::vector<std::string> tokens;
        {
            size_t start = 0;
            while (true) {
                size_t pos = line.find(kDelimUtf8, start);
                if (pos == std::string::npos) {
                    tokens.push_back(line.substr(start));
                    break;
                }
                tokens.push_back(line.substr(start, pos - start));
                start = pos + kDelimUtf8.size();
            }
        }

        if (tokens.size() < 6) {
            LOG(WARN) << "Failed to parse line: " << line;
            continue;
        }
        std::string const& path = tokens[0];
        std::string const& user = tokens[1];
        std::string const& group = tokens[2];
        std::string const& size_str = tokens[3];
        std::string const& perm_str = tokens[4];
        std::string const& sha1 = tokens[5];

        if (size_str == "error" || sha1 == "error") {
            LOG(WARN) << "Failed to get data: " << path;
            continue;
        }

        std::uintmax_t size{};
        try {
            size = static_cast<std::uintmax_t>(std::stoull(size_str));
        } catch (std::exception const& e) {
            LOG(WARN) << "Failed to get size: " << path << " - " << size_str
                      << " - not convertible: " << e.what();
            continue;
        }

        unsigned perm{};
        try {
            perm = static_cast<unsigned>(std::stoul(perm_str));
        } catch (std::exception const& e) {
            LOG(WARN) << "Failed to get permissions: " << path << " - "
                      << perm_str << " - not convertible: " << e.what();
            continue;
        }

        result.emplace_back(path, user, group, perm, size, sha1);
    }

    std::sort(result.begin(), result.end(),
              [](auto const& a, auto const& b) { return a.path < b.path; });

    return DefaultImageFiles{result};
}

inline void DefaultImageFiles::save(std::ostream& dst) const {
    for (auto const& info : files_) {
        dst << info.path << kDelimUtf8;
        dst << info.user << kDelimUtf8;
        dst << info.group << kDelimUtf8;
        dst << info.size << kDelimUtf8;
        dst << info.permissions << kDelimUtf8;
        dst << info.sha1 << '\n';
    }
}

#endif // DEFAULT_IMAGE_FILES_H
