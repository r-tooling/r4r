#include "logger.h"
#include <gtest/gtest.h>

class LoggerTest : public ::testing::Test {
  protected:
    void TearDown() override { Logger::get().set_sink<ConsoleSink>(); }
};

TEST_F(LoggerTest, BasicLogging) {
    auto* sink = Logger::get().set_sink<StoreSink>();
    LOG(INFO) << "Test message";
    auto messages = sink->get_messages();
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages[0].message, "Test message");
    EXPECT_EQ(messages[0].level, LogLevel::Info);
}

TEST_F(LoggerTest, LevelFiltering) {
    auto* sink = Logger::get().set_sink<StoreSink>();
    Logger::get().disable(LogLevel::Debug);
    LOG(DEBUG) << "Shouldn't appear";
    LOG(INFO) << "Should appear";
    auto messages = sink->get_messages();
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages[0].message, "Should appear");
    EXPECT_EQ(messages[0].level, LogLevel::Info);
}

TEST_F(LoggerTest, FatalAborts) {
    Logger::get().set_sink<ConsoleSink>();
    ASSERT_DEATH({ LOG(FATAL) << "Abort!"; }, "Abort!");
}

TEST_F(LoggerTest, CheckMacro) {
    Logger::get().set_sink<ConsoleSink>();
    ASSERT_DEATH(
        { CHECK(2 + 2 == 5) << "Math broken"; },
        "Check failed: 2 \\+ 2 == 5 Math broken");
}

TEST_F(LoggerTest, SinkReplacement) {
    auto* sink = Logger::get().set_sink<StoreSink>();
    LOG(INFO) << "New sink";
    ASSERT_EQ(sink->get_messages().size(), 1);
}

TEST_F(LoggerTest, EnableUpToLevel) {
    auto* sink = Logger::get().set_sink<StoreSink>();
    Logger::get().max_level(LogLevel::Info);

    EXPECT_FALSE(Logger::get().is_enabled(LogLevel::Trace));
    EXPECT_FALSE(Logger::get().is_enabled(LogLevel::Debug));
    EXPECT_TRUE(Logger::get().is_enabled(LogLevel::Info));
    EXPECT_TRUE(Logger::get().is_enabled(LogLevel::Warning));
    EXPECT_TRUE(Logger::get().is_enabled(LogLevel::Error));
    EXPECT_TRUE(Logger::get().is_enabled(LogLevel::Fatal));

    LOG(TRACE) << "1";
    LOG(DEBUG) << "2";
    LOG(INFO) << "3";
    LOG(WARN) << "4";

    auto messages = sink->get_messages();
    ASSERT_EQ(messages.size(), 2);
    EXPECT_EQ(messages[0].message, "3");
    EXPECT_EQ(messages[1].message, "4");
}
