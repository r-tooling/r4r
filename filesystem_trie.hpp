#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace util {

template <typename T>
class FileSystemTrie {
    struct Node {
        std::unordered_map<std::string, std::shared_ptr<Node>> children;
        T const* value;

        explicit Node(T const* value) : value(value){};
    };

    std::unordered_set<T> unique_values_;
    T const* default_value_;
    std::shared_ptr<Node> root_;

  public:
    explicit FileSystemTrie(T const& default_value) : default_value_{nullptr} {
        auto it = unique_values_.insert(default_value);
        default_value_ = &*(it.first);
        root_ = std::make_shared<Node>(default_value_);
    }

    T const* default_value() const { return default_value_; }

    void insert(fs::path const& path, T const& value) {
        auto node = root_;

        for (auto& path_part : path) {
            auto part = path_part.string();
            if (part.empty()) {
                continue;
            }

            if (!node->children.count(part)) {
                node->children[part] = std::make_shared<Node>(default_value_);
            }

            node = node->children[part];
        }

        // FIXME: Handle the case when this is already set
        // ideally there should be some closure which
        // would handle it? checking if it is a directory or
        // a file? reporting an error if it is a file?

        auto it = unique_values_.insert(value);
        node->value = &*(it.first);
    }

    [[nodiscard]] T const* find(fs::path const& path) const {
        auto node = root_;

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

        return node->value;
    }

    [[nodiscard]] T const* find_last_matching(fs::path const& path) const {
        auto node = root_;

        for (const auto& it : path) {
            auto part = it.string();

            if (part.empty()) {
                continue;
            }

            if (!node->children.contains(part)) {
                return node->value;
            }

            node = node->children[part];
        }

        return node->value;
    }

    bool is_empty() { return root_->children.empty(); }
};

} // namespace util
