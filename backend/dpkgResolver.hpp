#pragma once
#include "aptResolver.hpp"
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>

// FIXME: move to standalone file
namespace util {

template <typename T>
class FilesystemTrie {
  private:
    struct Node {
        std::unordered_map<std::string, std::shared_ptr<Node>> children;
        T package_name;
    };

  public:
    FilesystemTrie() : root(std::make_shared<Node>()) {}

    void insert(std::string const& path, T const& package_name) {
        auto node = root;
        std::istringstream iss(path);
        std::string part;

        while (std::getline(iss, part, '/')) {
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
        node->package_name = package_name;
    }

    std::optional<std::string> find(const std::string& path) const {
        auto node = root;
        std::istringstream iss(path);
        std::string part;
        while (std::getline(iss, part, '/')) {
            if (part.empty()) {
                continue;
            }
            if (!node->children.count(part)) {
                return {};
            }
            node = node->children[part];
        }
        return node->package_name;
    }

  private:
    std::shared_ptr<Node> root;
};

} // namespace util

namespace backend {

struct DpkgPackage {
    std::string packageName;
    std::string packageVersion;
    std::string aptVersion;
};

class DpkgDatabase {

  private:
};

} // namespace backend
