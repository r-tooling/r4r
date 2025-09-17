#include "filesystem_trie.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <vector>

#include <algorithm>

TEST(FileSystemTrieTest, DefaultInitialization) {
    FileSystemTrie<std::string> trie;
    EXPECT_TRUE(trie.is_empty());
}

TEST(FileSystemTrieTest, EmptyTrie) {
    FileSystemTrie<std::string> trie;
    EXPECT_EQ(trie.find("/a"), nullptr);
    EXPECT_EQ(trie.find_last_matching("/a/b"), nullptr);
}

TEST(FileSystemTrieTest, IgnoreRoot) {
    FileSystemTrie<bool> trie;

    trie.insert("/", true);
    trie.insert("/foo", false);

    EXPECT_EQ(*trie.find("/"), true);
    EXPECT_EQ(*trie.find("/foo"), false);
    EXPECT_EQ(*trie.find_last_matching("/bar"), true);
    EXPECT_EQ(*trie.find_last_matching("/foo/bar"), false);
}

TEST(FileSystemTrieTest, InsertAndFind) {
    FileSystemTrie<std::string> trie;
    trie.insert("/a/b/c", "value1");
    trie.insert("/a/b/d", "value2");

    auto const* result1 = trie.find("/a/b/c");
    ASSERT_NE(result1, nullptr);
    EXPECT_EQ(*result1, "value1");

    auto const* result2 = trie.find("/a/b/d");
    ASSERT_NE(result2, nullptr);
    EXPECT_EQ(*result2, "value2");

    auto const* result3 = trie.find("/a/b/e");
    EXPECT_EQ(result3, nullptr);
}

TEST(FileSystemTrieTest, OverwritePrevention) {
    FileSystemTrie<std::string> trie;
    trie.insert("/a/b", "value1");
    trie.insert("/a/b", "value2");

    auto const* result = trie.find("/a/b");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, "value2");
}

TEST(FileSystemTrieTest, DeepHierarchy) {
    FileSystemTrie<std::string> trie;
    trie.insert("/a/b/c/d/e/f", "value");

    auto const* result = trie.find("/a/b/c/d/e/f");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, "value");

    auto const* result_nonexistent = trie.find("/a/b/c/d/e/f/g");
    EXPECT_EQ(result_nonexistent, nullptr);
}

TEST(FileSystemTrieTest, EmptyPathHandling) {
    FileSystemTrie<std::string> trie;
    trie.insert("", "value");

    auto const* result = trie.find("");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, "value");
}

TEST(FileSystemTrieTest, RootNodePersistence) {
    FileSystemTrie<std::string> trie;
    trie.insert("/", "root_value");

    auto const* result = trie.find("/");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, "root_value");

    EXPECT_FALSE(trie.is_empty());
}

TEST(FileSystemTrieTest, DefaultValuePropagation) {
    FileSystemTrie<int> trie;
    trie.insert("/a/b/c", 1);

    auto const* r = trie.find("/a/b/c/d");
    EXPECT_EQ(r, nullptr);

    r = trie.find("/a/b/c");
    EXPECT_EQ(*r, 1);

    r = trie.find_last_matching("/a/b/c/d");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(*r, 1);

    r = trie.find_last_matching("/a/b");
    ASSERT_EQ(r, nullptr);
}

TEST(FileSystemTrieTest, UniqueValueStorage) {
    FileSystemTrie<std::string> trie;
    trie.insert("/a", "value1");
    trie.insert("/b", "value1");
    trie.insert("/c", "value2");

    // Only two unique values should exist
    EXPECT_EQ(trie.find("/a"), trie.find("/b"));
    EXPECT_NE(trie.find("/a"), trie.find("/c"));
}

TEST(FileSystemTrieTest, FindLastMatching) {
    FileSystemTrie<bool> trie;

    trie.insert("/dev", true);
    trie.insert("/dev/null", false);

    EXPECT_TRUE(*trie.find("/dev"));
    EXPECT_FALSE(*trie.find("/dev/null"));
    EXPECT_EQ(trie.find("/dev/tty"), nullptr);

    EXPECT_TRUE(*trie.find_last_matching("/dev"));
    EXPECT_FALSE(*trie.find_last_matching("/dev/null"));
    EXPECT_TRUE(trie.find_last_matching("/dev/tty"));
}

// Test that an empty trie yields no nodes.
TEST(FileSystemTrieTest, IteratorEmptyTrie) {
    FileSystemTrie<int> trie;
    auto begin = trie.begin();
    auto end = trie.end();
    EXPECT_EQ(begin, end);
}

// Test that the iterator visits all nodes (excluding the root) correctly.
TEST(FileSystemTrieIteratorTest, IteratorVisitsAllNodes) {
    FileSystemTrie<int> trie;

    trie.insert("a/b", 1);
    trie.insert("a/c", 2);
    trie.insert("d", 3);
    trie.insert("e", 4);

    std::vector<std::pair<fs::path, int>> actual;
    auto exists = [&](auto const& elem) {
        // auto it = std::find(actual.begin(), actual.end(), elem);
        auto it =
            std::find_if(actual.begin(), actual.end(), [&](auto const& pair) {
                return pair.first == elem.first && pair.second == elem.second;
            });
        return it != actual.end();
    };
    for (auto const& node : trie) {
        ASSERT_NE(node.value, nullptr);
        actual.emplace_back(node.path, *(node.value));
    }

    EXPECT_EQ(actual.size(), 4);
    EXPECT_TRUE(exists(std::pair<fs::path, int>{"a/b", 1}));
    EXPECT_TRUE(exists(std::pair<fs::path, int>{"a/c", 2}));
    EXPECT_TRUE(exists(std::pair<fs::path, int>{"d", 3}));
    EXPECT_TRUE(exists(std::pair<fs::path, int>{"e", 4}));
}

TEST(FileSystemTrieCopyConstructorTest, DeepCopy) {
    struct A {
        int n;
        auto operator<=>(A const& other) const = default;
    };
    FileSystemTrie<A> orig;

    orig.insert("a", A{1});
    orig.insert("a/b", A{2});
    orig.insert("a/c", A{3});
    orig.insert("d", A{4});

    FileSystemTrie<A> copy(orig);

    auto orig_nodes = std::vector(orig.begin(), orig.end());
    auto copy_nodes = std::vector(copy.begin(), copy.end());

    EXPECT_EQ(orig_nodes, copy_nodes);

    EXPECT_EQ(orig_nodes.size(), copy_nodes.size());
    for (size_t i = 0; i < orig.size(); i++) {
        // the values must be different
        EXPECT_NE(orig_nodes[i].value, copy_nodes[i].value);
        // the value must be the same
        EXPECT_EQ(*orig_nodes[i].value, *copy_nodes[i].value);
    }

    orig.insert("a/b", A{5});

    auto updated_orig_nodes = std::vector(orig.begin(), orig.end());
    EXPECT_NE(updated_orig_nodes, copy_nodes);

    auto copy_nodes2 = std::vector(copy.begin(), copy.end());
    EXPECT_EQ(copy_nodes2, copy_nodes);
}

TEST(FileSystemTrieTest, SizeMethod) {
    FileSystemTrie<std::string> trie;
    EXPECT_EQ(trie.size(), 0);

    trie.insert("/a", "value1");
    EXPECT_EQ(trie.size(), 1);

    trie.insert("/b", "value2");
    EXPECT_EQ(trie.size(), 2);

    trie.insert("/a/b/c", "value3");
    EXPECT_EQ(trie.size(), 3);

    trie.insert("/a/b/d", "value4");
    EXPECT_EQ(trie.size(), 4);

    trie.insert("/a/b/c", "new_value3");
    EXPECT_EQ(trie.size(), 4);
}
