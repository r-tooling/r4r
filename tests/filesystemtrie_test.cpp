#include "../filesystemtrie.hpp" // Replace with actual path to your header file
#include <gtest/gtest.h>

class FileSystemTrieTest : public ::testing::Test {
  protected:
    util::FileSystemTrie<std::string> trie{"default"};
};

TEST_F(FileSystemTrieTest, DefaultInitialization) {
    EXPECT_EQ(*trie.default_value(), "default");
    EXPECT_TRUE(trie.is_empty());
}

TEST_F(FileSystemTrieTest, InsertAndFind) {
    trie.insert("/a/b/c", "value1");
    trie.insert("/a/b/d", "value2");

    auto result1 = trie.find("/a/b/c");
    ASSERT_NE(result1, nullptr);
    EXPECT_EQ(*result1, "value1");

    auto result2 = trie.find("/a/b/d");
    ASSERT_NE(result2, nullptr);
    EXPECT_EQ(*result2, "value2");

    auto result3 = trie.find("/a/b/e");
    EXPECT_EQ(result3, nullptr);
}

TEST_F(FileSystemTrieTest, OverwritePrevention) {
    trie.insert("/a/b", "value1");

    trie.insert("/a/b", "value2");

    auto result = trie.find("/a/b");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, "value2");
}

TEST_F(FileSystemTrieTest, DeepHierarchy) {
    trie.insert("/a/b/c/d/e/f", "value");

    auto result = trie.find("/a/b/c/d/e/f");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, "value");

    auto result_nonexistent = trie.find("/a/b/c/d/e/f/g");
    EXPECT_EQ(result_nonexistent, nullptr);
}

TEST_F(FileSystemTrieTest, EmptyPathHandling) {
    trie.insert("", "value");

    auto result = trie.find("");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, "value");
}

TEST_F(FileSystemTrieTest, RootNodePersistence) {
    trie.insert("/", "root_value");

    auto result = trie.find("/");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, "root_value");

    EXPECT_FALSE(trie.is_empty());
}

TEST_F(FileSystemTrieTest, DefaultValuePropagation) {
    trie.insert("/a/b/c", "value1");

    auto result1 = trie.find("/a/b/c/d");
    EXPECT_EQ(result1, nullptr);

    auto last_matching = trie.find_last_matching("/a/b/c/d");
    ASSERT_NE(last_matching, nullptr);
    EXPECT_EQ(*last_matching, "value1");
}

TEST_F(FileSystemTrieTest, UniqueValueStorage) {
    trie.insert("/a", "value1");
    trie.insert("/b", "value1");
    trie.insert("/c", "value2");

    // Only two unique values should exist
    EXPECT_EQ(trie.find("/a"), trie.find("/b"));
    EXPECT_NE(trie.find("/a"), trie.find("/c"));
}

TEST_F(FileSystemTrieTest, FindLastMatching) {
    util::FileSystemTrie<bool> trie{false};

    trie.insert("/dev", true);
    trie.insert("/dev/null", false);

    //    auto p = [](bool value) { return value; };

    EXPECT_TRUE(*trie.find("/dev"));
    EXPECT_FALSE(*trie.find("/dev/null"));
    EXPECT_EQ(trie.find("/dev/tty"), nullptr);

    EXPECT_TRUE(*trie.find_last_matching("/dev"));
    EXPECT_FALSE(*trie.find_last_matching("/dev/null"));
    EXPECT_TRUE(*trie.find_last_matching("/dev/tty"));
}
