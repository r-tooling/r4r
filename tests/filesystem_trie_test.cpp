#include "filesystem_trie.h"
#include <gtest/gtest.h>
#include <vector>

TEST(FileSystemTrieTest, DefaultInitialization) {
    FileSystemTrie<std::string> trie;
    EXPECT_TRUE(trie.is_empty());
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

    trie.insert("a", 1);
    trie.insert("a/b", 2);
    trie.insert("a/c", 3);
    trie.insert("d", 4);

    std::vector<std::pair<fs::path, int>> actual;
    auto exists = [&](auto const& elem) {
        auto it = std::find(actual.begin(), actual.end(), elem);
        return it != actual.end();
    };

    for (auto const& node : trie) {
        ASSERT_NE(node.value, nullptr);
        actual.emplace_back(node.path, *(node.value));
    }

    EXPECT_EQ(actual.size(), 4);
    EXPECT_TRUE(exists(std::pair<fs::path, int>{"a", 1}));
}

TEST(FileSystemTrieCopyConstructorTest, DeepCopy) {
    FileSystemTrie<int> orig;

    orig.insert("a", 1);
    orig.insert("a/b", 2);
    orig.insert("a/c", 3);
    orig.insert("d", 4);

    FileSystemTrie<int> copy(orig);

    auto orig_nodes = std::vector(orig.begin(), orig.end());
    auto copy_nodes = std::vector(copy.begin(), copy.end());

    EXPECT_EQ(orig_nodes, copy_nodes);

    orig.insert("a/b", 5);

    auto updated_orig_nodes = std::vector(orig.begin(), orig.end());
    EXPECT_NE(updated_orig_nodes, copy_nodes);

    auto copy_nodes2 = std::vector(copy.begin(), copy.end());
    EXPECT_EQ(copy_nodes2, copy_nodes);
}
