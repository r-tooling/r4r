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
        return path == other.path && user == other.user &&
               group == other.group && permissions == other.permissions &&
               size == other.size && sha1 == other.sha1;
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

    static DefaultImageFiles from_file(fs::path const& path) {
        LOG_DEBUG(log_) << "Loading default file list from file: " << path;

        std::ifstream file(path);

        if (!file) {
            throw std::runtime_error("Failed to open file: " + path.string());
        }

        return from_stream(file);
    }

    static DefaultImageFiles
    from_image(std::string const& image_name,
               std::vector<std::string> const& blacklist_patterns) {
        LOG_DEBUG(log_) << "Loading default file list from image: "
                        << image_name;

        std::string bf_pattern = string_join(blacklist_patterns, '|');

        // clang-format off
        std::vector<std::string> docker_cmd = {
            "docker",
            "run",
            "--rm",
            image_name,
            "bash",
            "-c",
            STR(
               "DELIM='" NBSP "' " << "BF_PATTERN='" << bf_pattern << "' " <<
                R""(
                find / -type f 2>/dev/null | grep -vE "$BF_PATTERN" | while IFS= read -r file; do
                    stat="$(stat -c "%U${DELIM}%G${DELIM}%s${DELIM}%a" "$file" 2>/dev/null || echo "error${DELIM}error${DELIM}error${DELIM}error")"
                    sha1="$((sha1sum "$file" 2>/dev/null | cut -d " " -f1) || echo "error")"
                    echo "$file${DELIM}${stat}${DELIM}${sha1}"
                done
            )"")};
        // clang-format on

        Process proc{docker_cmd};
        auto result = from_stream(proc.output());

        if (proc.wait() != 0) {
            throw std::runtime_error(
                STR("Unable to initialize default file list for "
                    << image_name
                    << "\nCommand: " << string_join(docker_cmd, ' ')));
        }

        return result;
    }

    static DefaultImageFiles from_stream(std::istream& stream) {
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
                    } else {
                        tokens.push_back(line.substr(start, pos - start));
                        start = pos + kDelimUtf8.size();
                    }
                }
            }

            if (tokens.size() < 6) {
                LOG_WARN(log_) << "WARNING: Could not parse line: " << line;
                continue;
            }
            std::string const& path = tokens[0];
            std::string const& user = tokens[1];
            std::string const& group = tokens[2];
            std::string const& size_str = tokens[3];
            std::string const& perm_str = tokens[4];
            std::string const& sha1 = tokens[5];

            if (size_str == "error" || sha1 == "error") {
                LOG_WARN(log_) << "WARNING: " << path << ": error getting data";
                continue;
            }

            std::uintmax_t size;
            try {
                size = static_cast<std::uintmax_t>(std::stoull(size_str));
            } catch (...) {
                LOG_WARN(log_) << "WARNING: " << path
                               << ": size not convertable " << size_str;
                continue;
            }

            unsigned perm;
            try {
                perm = static_cast<unsigned>(std::stoul(perm_str));
            } catch (...) {
                LOG_WARN(log_) << "WARNING: " << path
                               << ": permission not convertable " << perm_str;
                continue;
            }

            result.emplace_back(path, user, group, perm, size, sha1);
        }

        std::sort(result.begin(), result.end(),
                  [](auto const& a, auto const& b) { return a.path < b.path; });

        return DefaultImageFiles{result};
    }

    [[nodiscard]] std::vector<ImageFileInfo> const& files() const {
        return files_;
    }

    size_t size() const { return files_.size(); }

    void save(std::ostream& dst) const {
        for (auto& info : files_) {
            dst << info.path << kDelimUtf8;
            dst << info.user << kDelimUtf8;
            dst << info.group << kDelimUtf8;
            dst << info.size << kDelimUtf8;
            dst << info.permissions << kDelimUtf8;
            dst << info.sha1 << '\n';
        }
    }

  private:
    static inline Logger& log_ = LogManager::logger("default-image-files");
    std::vector<ImageFileInfo> files_;
};

#endif // DEFAULT_IMAGE_FILES_H
