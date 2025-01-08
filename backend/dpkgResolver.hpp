#pragma once
#include "aptResolver.hpp"
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

// FIXME: move to some global definitions
namespace fs = std::filesystem;

// FIXME: move to standalone file
namespace util {

inline std::string executeCommand(const std::string& command) {
    std::array<char, 128> buffer{};
    std::string result;

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to run command: " + command);
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }

    if (pclose(pipe) != 0) {
        throw std::runtime_error("Command execution failed: " + command);
    }

    return result;
}

template <typename T>
class FilesystemTrie {
  private:
    struct Node {
        std::unordered_map<std::string, std::shared_ptr<Node>> children;
        T value;
    };

  public:
    FilesystemTrie() : root(std::make_shared<Node>()) {}

    void insert(fs::path const& path, T const& value) {
        auto node = root;
        std::istringstream iss(path);
        std::string part;

        for (auto& path_part : path) {
            auto part = path_part.string();
            if (part.empty()) {
                continue;
            }
            if (!node->children.count(part)) {
                node->children[part] = std::make_shared<Node>();
            }
            node = node->children[part];
        }

        // FIXME: Handle the case when this is already set
        // ideally there should be some closure which
        // would handle it? checking if it is a directory or
        // a file? reporting an error if it is a file?
        node->value = value;
    }

    [[nodiscard]] std::string const* find(fs::path const& path) const {
        auto node = root;
        std::istringstream iss(path);
        std::string part;

        for (auto& path_part : path) {
            auto part = path_part.string();

            if (part.empty()) {
                continue;
            }
            if (!node->children.count(part)) {
                return {};
            }
            node = node->children[part];
        }

        return &node->value;
    }

  private:
    std::shared_ptr<Node> root;
};

} // namespace util

namespace backend {

struct DebPackage {
    std::string name;
    std::string version;
};

class DpkgDatabase {
  public:
    static DpkgDatabase from_path(fs::path const& path = "/var/lib/dpkg/info/");

    DpkgDatabase(std::unordered_map<std::string, DebPackage> packages,
                 util::FilesystemTrie<std::string> files)
        : packages_{std::move(packages)}, files_{std::move(files)} {}

    DebPackage const* lookup(fs::path const& path) const;

  private:
    std::unordered_map<std::string, DebPackage> packages_;
    util::FilesystemTrie<std::string> files_;
};

} // namespace backend
