#ifndef FILESYSTEM_TRIE_H
#define FILESYSTEM_TRIE_H

#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

template <typename T>
class FileSystemTrie {
    struct Node {
        std::unordered_map<std::string, std::unique_ptr<Node>> children;
        T const* value;

        explicit Node(T const* value) : value(value) {};
        Node(Node const&) = delete;
        Node(Node&&) noexcept = delete;
        Node& operator=(Node const&) = delete;
        Node& operator=(Node&&) noexcept = delete;
    };

    struct NodeView {
        fs::path path;
        T const* value;

        bool operator==(NodeView const& other) const = default;
    };

    class ConstIterator {
        static inline NodeView const kEndSentinel{"", nullptr};
        std::vector<std::pair<fs::path, Node const*>> stack_;
        NodeView current_;

        void advance();

      public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = NodeView;
        using difference_type = std::ptrdiff_t;
        using pointer = NodeView const*;
        using reference = NodeView const&;

        /// Default constructor creates an "end" iterator.
        ConstIterator() : current_{kEndSentinel} {}

        /// Construct an iterator starting at a given root.
        explicit ConstIterator(Node const* root);

        reference operator*() const { return current_; }
        pointer operator->() const { return &current_; }

        ConstIterator& operator++() {
            advance();
            return *this;
        }

        bool operator==(ConstIterator const& other) const = default;
    };

    void insert(fs::path const& path, T const* value);

    std::set<T> unique_values_;
    std::unique_ptr<Node> root_{std::make_unique<Node>(nullptr)};

  public:
    FileSystemTrie() = default;

    FileSystemTrie(FileSystemTrie const& other)
        : unique_values_{other.unique_values_} {
        for (auto const& nodeInfo : other) {
            this->insert(nodeInfo.path, nodeInfo.value);
        }
    };

    FileSystemTrie(FileSystemTrie&& other) noexcept = default;

    FileSystemTrie& operator=(FileSystemTrie const&) = delete;
    FileSystemTrie& operator=(FileSystemTrie&&) noexcept = default;

    ConstIterator begin() const { return ConstIterator{root_.get()}; }
    ConstIterator end() const { return ConstIterator{}; }

    void insert(fs::path const& path, T const& value);

    [[nodiscard]] T const* find(fs::path const& path) const;

    [[nodiscard]] T const* find_last_matching(fs::path const& path) const;

    bool is_empty() { return root_->children.empty(); }
};

template <typename T>
void FileSystemTrie<T>::ConstIterator::advance() {
    if (stack_.empty()) {
        current_ = kEndSentinel;
        return;
    }

    auto [path, node] = stack_.back();
    stack_.pop_back();

    current_.path = path;
    current_.value = node->value;

    for (auto const& [key, value] : node->children) {
        stack_.push_back({path / key, value.get()});
    }
}

template <typename T>
FileSystemTrie<T>::ConstIterator::ConstIterator(Node const* root) {
    assert(root != nullptr && "Node should not be null");

    for (auto const& [key, value] : root->children) {
        stack_.push_back({key, value.get()});
    }
    advance();
}

template <typename T>
void FileSystemTrie<T>::insert(fs::path const& path, T const* value) {
    Node* node = root_.get();

    for (auto const& path_part : path) {
        auto part = path_part.string();

        if (part.empty()) {
            continue;
        }

        auto [it, _] =
            node->children.try_emplace(part, std::make_unique<Node>(nullptr));
        node = it->second.get();
    }

    // FIXME: Handle the case when this is already set
    // ideally there should be some closure which
    // would handle it? checking if it is a directory or
    // a file? reporting an error if it is a file?
    node->value = value;
}

template <typename T>
void FileSystemTrie<T>::insert(fs::path const& path, T const& value) {
    auto [it, _] = unique_values_.insert(std::move(value));
    insert(path, &(*it));
}

template <typename T>
T const* FileSystemTrie<T>::find(fs::path const& path) const {
    Node* node = root_.get();

    for (auto const& path_part : path) {
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

template <typename T>
T const* FileSystemTrie<T>::find_last_matching(fs::path const& path) const {
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

#endif // FILESYSTEM_TRIE_H
