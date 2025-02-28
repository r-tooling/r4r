#ifndef CONFIG_FILE_
#define CONFIG_FILE_

#include "common.h"
#include "logger.h"

#include <filesystem>
#include <fstream>
#include <istream>
#include <optional>
#include <string>
#include <unordered_map>

class ConfigFile {
  public:
    ConfigFile(std::istream& file) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }

            auto pos = line.find('=');
            if (pos == std::string::npos) {
                continue;
            }

            auto key = line.substr(0, pos);
            auto value = line.substr(pos + 1);

            // If value is surrounded by quotes, remove them
            if (value.size() >= 2 && value.front() == '"' &&
                value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }

            config_[key] = value;
        }
    }

    static inline std::optional<ConfigFile> from_file(fs::path path) {
        std::ifstream input{path};
        if (!input) {
            return {};
        }
        return ConfigFile{input};
    }

    std::string const& operator[](std::string const& key) const {
        return config_.at(key);
    }

    std::string const& get(std::string const& key) const {
        return config_.at(key);
    }

  private:
    static inline std::unordered_map<std::string, std::string> config_;
};

#endif // CONFIG_FILE_