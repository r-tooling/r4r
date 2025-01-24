#include "../logger.hpp"
#include <gtest/gtest.h>
#include <regex>
#include <sstream>

TEST(LoggerTest, BasicLogging) {
    std::stringstream ss;
    Logger logger("TestLogger");
    auto sink = std::make_shared<std::ostream>(ss.rdbuf());
    logger.add_sink(sink);

    logger.set_pattern("{logger} {level}: {message}");
    LOG_INFO(logger) << "Test message";
    ASSERT_EQ(ss.str(), "TestLogger INFO: Test message\n");
}

TEST(LoggerTest, MultipleSinks) {
    std::stringstream ss1;
    std::stringstream ss2;
    Logger logger("TestLogger");
    auto sink1 = std::make_shared<std::ostream>(ss1.rdbuf());
    auto sink2 = std::make_shared<std::ostream>(ss2.rdbuf());
    logger.add_sink(sink1);
    logger.add_sink(sink2);

    logger.set_pattern("{level} - {message}");
    LOG_DEBUG(logger) << "Debug message";
    ASSERT_EQ(ss1.str(), "DEBUG - Debug message\n");
    ASSERT_EQ(ss2.str(), "DEBUG - Debug message\n");
}

TEST(LoggerTest, PatternParsing) {
    std::stringstream ss;
    Logger logger("TestLogger");
    auto sink = std::make_shared<std::ostream>(ss.rdbuf());
    logger.add_sink(sink);

    logger.set_pattern("[{logger}]({level}) {message}");
    LOG_WARN(logger) << "Warning message";
    ASSERT_EQ(ss.str(), "[TestLogger](WARN) Warning message\n");

    ss.str("");

    logger.set_pattern(
        "Prefix: {logger} - {level} - {message} - [{elapsed_time}]");
    LOG_INFO(logger) << "Complex test";

    std::regex pattern(
        R"(Prefix: TestLogger - INFO - Complex test - \[\d+ms\]\n)");
    ASSERT_TRUE(std::regex_match(ss.str(), pattern));
}

TEST(LoggerTest, ChangePattern) {
    std::stringstream ss;
    Logger logger("TestLogger");
    auto sink = std::make_shared<std::ostream>(ss.rdbuf());
    logger.add_sink(sink);

    logger.set_pattern("{logger} {message}");
    LOG_INFO(logger) << "First message";
    ASSERT_EQ(ss.str(), "TestLogger First message\n");

    ss.str("");

    logger.set_pattern("{level}: {message} ({logger})");
    LOG_WARN(logger) << "Second message";
    ASSERT_EQ(ss.str(), "WARN: Second message (TestLogger)\n");
}

TEST(LoggerTest, LevelSpecificPattern) {
    std::stringstream ss;
    Logger logger("TestLogger");
    auto sink = std::make_shared<std::ostream>(ss.rdbuf());
    logger.add_sink(sink);

    logger.set_pattern("{message}"); // Default pattern
    logger.set_pattern(LogLevel::Warn, "Warning: {message} ({logger})");

    LOG_INFO(logger) << "Regular message";
    ASSERT_EQ(ss.str(), "Regular message\n");
    ss.str("");

    LOG_WARN(logger) << "Important message";
    ASSERT_EQ(ss.str(), "Warning: Important message (TestLogger)\n");
}

TEST(LoggerTest, ColorCodes) {
    std::stringstream ss;
    Logger logger("TestLogger");
    auto sink = std::make_shared<std::ostream>(ss.rdbuf());
    logger.add_sink(sink);

    logger.set_pattern("{fg_red}{level}{fg_reset}: {message}");
    LOG_INFO(logger) << "Colored message";

    std::string expected = "\033[31mINFO\033[0m: Colored message\n";
    ASSERT_EQ(ss.str(), expected);
}

TEST(LoggerTest, EscapingBraces) {
    std::stringstream ss;
    Logger logger("TestLogger");
    auto sink = std::make_shared<std::ostream>(ss.rdbuf());
    logger.add_sink(sink);

    logger.set_pattern("{{ {level}");
    LOG_INFO(logger) << "Test message";

    ASSERT_EQ(ss.str(), "{ INFO\n");

    ss.str("");
    logger.set_pattern("{{{{");
    LOG_INFO(logger) << "Test";
    ASSERT_EQ(ss.str(), "{{\n");
}
