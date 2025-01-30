#include "logger.h"
#include <gtest/gtest.h>
#include <regex>
#include <sstream>

// TEST(LoggerTest, BasicLogging) {
//     std::stringstream s;
//     Logger log("TestLogger", s, "{logger} {level}: {message}");gg
//     LOG_INFO(log) << "Test message";
//     ASSERT_EQ(s.str(), "TestLogger INFO: Test message\n");
// }
//
// TEST(LoggerTest, LevelSpecificSink) {
//     std::stringstream s1;
//     std::stringstream s2;
//
//     Logger log("TestLogger", "{level} - {message}");
//     log.set_sink(LogLevel::Info, s1);
//     log.set_sink(LogLevel::Debug, s2);
//
//     LOG_INFO(log) << "message1";
//     ASSERT_EQ(s1.str(), "INFO - message1\n");
//     ASSERT_TRUE(s2.str().empty());
//     LOG_DEBUG(log) << "message2";
//     ASSERT_EQ(s1.str(), "INFO - message1\n");
//     ASSERT_EQ(s2.str(), "DEBUG - message2\n");
// }
//
// TEST(LoggerTest, PatternParsing) {
//     std::stringstream s;
//     Logger log("TestLogger", s);
//
//     log.set_pattern("[{logger}]({level}) {message}");
//     LOG_WARN(log) << "Warning message";
//     ASSERT_EQ(s.str(), "[TestLogger](WARN) Warning message\n");
//
//     s.str("");
//
//     log.set_pattern(
//         "Prefix: {logger} - {level} - {message} - [{elapsed_time}]");
//     LOG_INFO(log) << "Complex test";
//
//     std::regex pattern(
//         R"(Prefix: TestLogger - INFO - Complex test - \[\d+ms\]\n)");
//     ASSERT_TRUE(std::regex_match(s.str(), pattern));
// }
//
// TEST(LoggerTest, LevelSpecificPattern) {
//     std::stringstream s;
//     Logger log("TestLogger", s);
//
//     log.set_pattern("{message}");
//     log.set_pattern(LogLevel::Warn, "Warning: {message} ({logger})");
//
//     LOG_INFO(log) << "Regular message";
//     ASSERT_EQ(s.str(), "Regular message\n");
//     s.str("");
//
//     LOG_WARN(log) << "Important message";
//     ASSERT_EQ(s.str(), "Warning: Important message (TestLogger)\n");
// }
//
// TEST(LoggerTest, ColorCodes) {
//     std::stringstream s;
//     Logger log("TestLogger", s);
//
//     log.set_pattern("{fg_red}{level}{fg_reset}: {message}");
//     LOG_INFO(log) << "Colored message";
//
//     std::string expected = "\033[31mINFO\033[0m: Colored message\n";
//     ASSERT_EQ(s.str(), expected);
// }
//
// TEST(LoggerTest, EscapingBraces) {
//     std::stringstream s;
//     Logger log("TestLogger", s);
//
//     log.set_pattern("{{ {level}");
//     LOG_INFO(log) << "Test message";
//
//     ASSERT_EQ(s.str(), "{ INFO\n");
//
//     s.str("");
//     log.set_pattern("{{{{");
//     LOG_INFO(log) << "Test";
//     ASSERT_EQ(s.str(), "{{\n");
// }
//
// TEST(LoggerTest, EnablingAndDisablingLevels) {
//     std::stringstream s;
//     Logger log("TestLogger", s, "{level}");
//
//     auto log_all = [&log]() {
//         LOG_DEBUG(log) << "";
//         LOG_INFO(log) << "";
//         LOG_WARN(log) << "";
//         LOG_ERROR(log) << "";
//     };
//
//     log_all();
//     ASSERT_EQ(s.str(), "DEBUG\nINFO\nWARN\nERROR\n");
//     s.str("");
//
//     log.disable(LogLevel::Debug);
//     log_all();
//     ASSERT_EQ(s.str(), "INFO\nWARN\nERROR\n");
//     s.str("");
//
//     log.disable(LogLevel::Warn);
//     log_all();
//     ASSERT_EQ(s.str(), "INFO\nERROR\n");
//     s.str("");
//
//     log.enable(LogLevel::Debug);
//     log_all();
//     ASSERT_EQ(s.str(), "DEBUG\nINFO\nERROR\n");
//     s.str("");
//
//     log.enable(LogLevel::Warn);
//     log_all();
//     ASSERT_EQ(s.str(), "DEBUG\nINFO\nWARN\nERROR\n");
//     s.str("");
//
//     auto fail = []() {
//         assert(false);
//         return "never return";
//     };
//     log.disable(LogLevel::Debug);
//     LOG_DEBUG(log) << fail();
// }
