#include "logger.h"
#include <gtest/gtest.h>
#include <memory>

class LoggerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto new_sink = std::make_unique<StoreSink>();
        sink = new_sink.get();
        old_sink_ = Logger::get().set_sink(std::move(new_sink));
    }
    void TearDown() override { Logger::get().set_sink(std::move(old_sink_)); }

    StoreSink* sink;

  private:
    std::unique_ptr<LogSink> old_sink_;
};

TEST_F(LoggerTest, BasicLogging) {
    LOG(INFO) << "Test message";
    auto messages = sink->get_messages();
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages[0].message, "Test message");
    EXPECT_EQ(messages[0].level, LogLevel::Info);
}

TEST_F(LoggerTest, LevelFiltering) {
    Logger::get().disable(LogLevel::Debug);
    LOG(DEBUG) << "Shouldn't appear";
    LOG(INFO) << "Should appear";
    auto messages = sink->get_messages();
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages[0].message, "Should appear");
    EXPECT_EQ(messages[0].level, LogLevel::Info);
}

TEST_F(LoggerTest, FatalAborts) {
    Logger::get().set_sink(std::make_unique<ConsoleSink>());
    ASSERT_DEATH({ LOG(FATAL) << "Abort!"; }, "Abort!");
}

TEST_F(LoggerTest, CheckMacro) {
    Logger::get().set_sink(std::make_unique<ConsoleSink>());
    ASSERT_DEATH(
        { CHECK(2 + 2 == 5) << "Math broken"; },
        "Check failed: 2 \\+ 2 == 5 Math broken");
}

TEST_F(LoggerTest, EnableUpToLevel) {
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
