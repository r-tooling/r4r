#ifndef FILESYSTEM_TRIE_H
#define FILESYSTEM_TRIE_H

#include <cassert>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

template <typename T>
class FileSystemTrie {
    struct Node {
        std::unordered_map<std::string, std::unique_ptr<Node>> children;
        std::optional<T const*> value;

        Node() : value{} {};
        explicit Node(T const* value) : value{value} {};
        Node(Node const&) = delete;
        Node(Node&&) noexcept = delete;
        Node& operator=(Node const&) = delete;
        Node& operator=(Node&&) noexcept = delete;
    };

    struct NodeView {
        fs::path path;
        T const* value;

        bool operator==(NodeView const& other) const {
            if (path != other.path) {
                return false;
            }
            if (value == nullptr) {
                return other.value == nullptr;
            }
            if (other.value == nullptr) {
                return false;
            }
            return *value == *other.value;
        };
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
    std::unique_ptr<Node> root_{std::make_unique<Node>()};
    size_t size_{0};

  public:
    FileSystemTrie() = default;

    FileSystemTrie(FileSystemTrie const& other) : unique_values_{} {
        for (auto const& info : other) {
            this->insert(info.path, *info.value);
        }
    };

    FileSystemTrie(FileSystemTrie&& other) noexcept = default;

    FileSystemTrie& operator=(FileSystemTrie const& other) {
        size_ = 0;
        unique_values_.clear();

        for (auto const& info : other) {
            this->insert(info.path, *info.value);
        }
    }

    FileSystemTrie& operator=(FileSystemTrie&&) noexcept = default;

    ConstIterator begin() const { return ConstIterator{root_.get()}; }
    ConstIterator end() const { return ConstIterator{}; }

    void insert(fs::path const& path, T const& value);

    [[nodiscard]] T const* find(fs::path const& path) const;

    [[nodiscard]] T const* find_last_matching(fs::path const& path) const;

    bool is_empty() { return root_->children.empty(); }

    [[nodiscard]] size_t size() { return size_; }
};

template <typename T>
FileSystemTrie<T>::ConstIterator::ConstIterator(Node const* root) {
    assert(root != nullptr && "Node should not be null");

    stack_.push_back({"", root});

    advance();
}

template <typename T>
void FileSystemTrie<T>::ConstIterator::advance() {
    while (!stack_.empty()) {
        auto [path, node] = stack_.back();
        stack_.pop_back();

        for (auto const& [key, value] : node->children) {
            stack_.push_back({path / key, value.get()});
        }

        if (node->value) {
            current_.path = path;
            current_.value = *(node->value);
            return;
        }
    }

    current_ = kEndSentinel;
}

template <typename T>
void FileSystemTrie<T>::insert(fs::path const& path, T const* value) {
    Node* node = root_.get();
    bool inserted = false;

    for (auto const& path_part : path) {
        auto part = path_part.string();

        if (part.empty()) {
            continue;
        }

        auto [it, ins] =
            node->children.try_emplace(part, std::make_unique<Node>());
        node = it->second.get();
        inserted |= ins;
    }

    // FIXME: Handle the case when this is already set
    // ideally there should be some closure which
    // would handle it? checking if it is a directory or
    // a file? reporting an error if it is a file?
    node->value = value;
    if (inserted) {
        size_++;
    }
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
        if (!node->children.contains(part)) {
            return {};
        }
        node = node->children[part].get();
    }

    return node->value.value_or(nullptr);
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
            return node->value.value_or(nullptr);
        }

        node = node->children[part].get();
    }

    return node->value.value_or(nullptr);
}

#endif // FILESYSTEM_TRIE_H
