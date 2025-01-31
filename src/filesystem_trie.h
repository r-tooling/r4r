#ifndef FILESYSTEM_TRIE_H
#define FILESYSTEM_TRIE_H

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

template <typename T>
class FileSystemTrie {
    struct Node {
        std::unordered_map<std::string, std::unique_ptr<Node>> children;
        T const* value;

        explicit Node(T const* value) : value(value){};
        Node(Node const&) = delete;
        Node(Node&&) = default;
        Node& operator=(Node const&) = delete;
        Node& operator=(Node&&) = default;
    };

    std::unordered_set<T> unique_values_;
    T const* default_value_;
    std::unique_ptr<Node> root_;

  public:
    FileSystemTrie(FileSystemTrie const&) = delete;

    FileSystemTrie(FileSystemTrie&&) = default;

    FileSystemTrie()
        : default_value_(nullptr), root_(std::make_unique<Node>(nullptr)) {}

    explicit FileSystemTrie(T const& default_value) : default_value_{nullptr} {
        auto it = unique_values_.insert(default_value);
        default_value_ = &*(it.first);
        root_ = std::make_unique<Node>(default_value_);
    }

    FileSystemTrie& operator=(FileSystemTrie const&) = delete;
    FileSystemTrie& operator=(FileSystemTrie&&) = default;

    T const* default_value() const { return default_value_; }

    void insert(fs::path const& path, T const& value) {
        Node* node = root_.get();

        for (auto& path_part : path) {
            auto part = path_part.string();
            if (part.empty()) {
                continue;
            }

            if (!node->children.count(part)) {
                node->children[part] = std::make_unique<Node>(default_value_);
            }

            node = node->children[part].get();
        }

        // FIXME: Handle the case when this is already set
        // ideally there should be some closure which
        // would handle it? checking if it is a directory or
        // a file? reporting an error if it is a file?

        auto it = unique_values_.insert(value);
        node->value = &*(it.first);
    }

    [[nodiscard]] T const* find(fs::path const& path) const {
        Node* node = root_.get();

        for (auto& path_part : path) {
            auto part = path_part.string();

            if (part.empty()) {
                continue;
            }
            if (!node->children.count(part)) {
                return {};
            }
            node = node->children[part].get();
        }

        return node->value;
    }

    [[nodiscard]] T const* find_last_matching(fs::path const& path) const {
        Node* node = root_.get();

        for (auto const& it : path) {
            auto part = it.string();

            if (part.empty()) {
                continue;
            }

            if (!node->children.contains(part)) {
                return node->value;
            }

            node = node->children[part].get();
        }

        return node->value;
    }

    bool is_empty() { return root_->children.empty(); }
};

#endif // FILESYSTEM_TRIE_H
