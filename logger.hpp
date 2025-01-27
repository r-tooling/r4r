#pragma once

#include "common.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// TODO: upgrade to ubuntu:24.04
#ifdef USE_STD_FORMAT
#include <format>
#endif

using namespace std::string_view_literals;
using namespace std::string_literals;

typedef std::chrono::steady_clock logger_clock;

#define LOG_LEVELS                                                             \
    X(Debug, "DEBUG")                                                          \
    X(Info, "INFO")                                                            \
    X(Warn, "WARN")                                                            \
    X(Error, "ERROR")

enum class LogLevel {
#define X(a, b) a,
    LOG_LEVELS
#undef X
};

constexpr size_t LogLevelCount = static_cast<size_t>(LogLevel::Error) + 1;

struct LogEntry {
    LogLevel level;
    logger_clock::time_point time;
    std::string message;
};

#define LOG_PATTER_TOKEN_KINDS                                                 \
    X(Text, text)                                                              \
    X(Logger, logger)                                                          \
    X(Level, level)                                                            \
    X(ElapsedTime, elapsed_time)                                               \
    X(Message, message)                                                        \
    X(Color, color)

class LogPatternToken {
  public:
    enum class Kind {
#define X(a, b) a,
        LOG_PATTER_TOKEN_KINDS
#undef X
    };

    explicit LogPatternToken(Kind kind) : kind_{kind} {}
    explicit LogPatternToken(Kind kind, std::string payload)
        : kind_{kind}, payload_{std::move(payload)} {}

    static std::optional<LogPatternToken> from_string(std::string_view str) {
        if (str.starts_with("fg_")) {
            auto color = str.substr(3);
            auto it = fg_colors_.find(color);
            if (it != fg_colors_.end()) {
                return LogPatternToken{Kind::Color, std::string(it->second)};
            }
        }

        auto it = std::find(keywords_.begin(), keywords_.end(), str);
        if (it != keywords_.end() && *it != "color"sv) {
            auto kind = static_cast<Kind>(std::distance(keywords_.begin(), it));
            return LogPatternToken{kind};
        } else {
            return {};
        }
    }

    [[nodiscard]] Kind kind() const { return kind_; }

    [[nodiscard]] std::string payload() const {
        if (payload_.has_value()) {
            return *payload_;
        } else {
            throw std::runtime_error("Token does not have payload");
        }
    }

  private:
    Kind kind_;

    // the TEXT, COLOR kind has content
    std::optional<std::string> payload_;

    inline static const std::array keywords_ = {
#define X(a, b) #b##sv,
        LOG_PATTER_TOKEN_KINDS
#
#undef X
    };

    inline static const std::unordered_map<std::string_view, std::string_view>
        fg_colors_ = {{"red", "\033[31m"},    {"green", "\033[32m"},
                      {"blue", "\033[34m"},   {"cyan", "\033[36m"},
                      {"yellow", "\033[33m"}, {"magenta", "\033[35m"},
                      {"white", "\033[37m"},  {"reset", "\033[0m"}};

    friend std::ostream& operator<<(std::ostream& os,
                                    LogPatternToken const& token) {
        os << "LogPatternToken { kind: "
           << keywords_[static_cast<size_t>(token.kind_)];
        if (token.kind_ == Kind::Text && token.payload_.has_value()) {
            os << ", payload: \"" << *token.payload_ << "\"";
        }
        os << " }";
        return os;
    }
};

class LogPatternParser {
  public:
    explicit LogPatternParser(std::string_view pattern)
        : pos{pattern.begin()}, end{pattern.end()} {}

    std::vector<LogPatternToken> parse() {
        std::vector<LogPatternToken> tokens;
        while (auto token = next_token()) {
            tokens.push_back(*token);
        }
        return tokens;
    }

    std::optional<LogPatternToken> next_token() {
        char const* start = pos;

        while (!eof()) {
            char c = *pos;
            if (c == '{') {
                if (start != pos) {
                    break;
                }

                pos++;

                if (pos < end && *pos == '{') {
                    pos++;
                    return LogPatternToken{LogPatternToken::Kind::Text, "{"s};
                } else {
                    return LogPatternToken{keyword()};
                }
            } else {
                pos++;
            }
        }

        if (start != pos) {
            return text(start);
        } else {
            return {};
        }
    }

  private:
    LogPatternToken text(char const* start) {
        return LogPatternToken{LogPatternToken::Kind::Text,
                               std::string{start, pos}};
    }

    LogPatternToken keyword() {
        // start at the character after the '{'
        char const* start = pos;
        while (!eof() && *pos != '}') {
            pos++;
        }

        if (eof()) {
            throw std::runtime_error("Unexpected end of pattern");
        }

        std::string_view kw(start, pos - start);
        auto kind = LogPatternToken::from_string(kw);
        if (kind) {
            // past the closing '}'
            pos++;
            return *kind;
        } else {
#ifdef USE_STD_FORMAT
            throw std::runtime_error(std::format("Unknown keyword: {}", kw));
#else
            throw std::runtime_error(STR("Unknown keyword: " << kw));
#endif
        }
    }

    [[nodiscard]] bool eof() const { return pos == end; }

    const char* pos;
    const char* const end;
};

class Logger {
  public:
    explicit Logger(std::string name, std::ostream& sink = std::cout,
                    std::string const& pattern = "{message}")
        : name_(std::move(name)) {
        set_pattern(pattern);
        set_sink(sink);
    }

    Logger(std::string const& name, std::string const& pattern)
        : Logger{name, std::cout, pattern} {}

    Logger(std::string name, Logger const& log)
        : name_{name}, patterns_{log.patterns_}, sinks_{log.sinks_} {}

    void set_sink(LogLevel level, std::ostream& sink) {
        sinks_[static_cast<int>(level)] = &sink;
    }

    void set_sink(std::ostream& sink) {
        for (size_t i = 0; i < LogLevelCount; i++) {
            set_sink(static_cast<LogLevel>(i), sink);
        }
    }

    void set_pattern(LogLevel level, std::string const& pattern) {
        auto tokens = LogPatternParser(pattern).parse();
        patterns_[static_cast<int>(level)] = tokens;
    }

    void set_pattern(std::string const& pattern) {
        for (size_t i = 0; i < LogLevelCount; i++) {
            set_pattern(static_cast<LogLevel>(i), pattern);
        }
    }

    [[nodiscard]] bool is_enabled(LogLevel level) const {
        std::uint8_t n = 1 << static_cast<int>(level);
        return (enabled_levels_ & n) == n;
    }

    void enable(LogLevel level) {
        enabled_levels_ |= 1 << static_cast<int>(level);
    }

    void disable(LogLevel level) {
        enabled_levels_ ^= 1 << static_cast<int>(level);
    }

#ifdef USE_STD_FORMAT
    template <typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Info, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Warn, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Error, fmt, std::forward<Args>(args)...);
    }
#endif

  private:
#ifdef USE_STD_FORMAT
    template <typename... Args>
    void log(LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
        if (!is_enabled(level)) {
            return;
        }
        auto now = logger_clock::now();
        std::string message = std::format(fmt, args...);

        LogEntry entry{level, now, message};
        log(entry);
    }
#endif

    void log(LogEntry const& entry) {
        if (!is_enabled(entry.level)) {
            return;
        }
        auto output = sinks_[static_cast<int>(entry.level)];

        static const std::array levels = {
#define X(a, b) b,
            LOG_LEVELS
#undef X
        };

        auto const& pattern = patterns_[static_cast<int>(entry.level)];

        for (const auto& token : pattern) {
            switch (token.kind()) {
            case LogPatternToken::Kind::Text:
            case LogPatternToken::Kind::Color:
                *output << token.payload();
                break;
            case LogPatternToken::Kind::Logger:
                *output << name_;
                break;
            case LogPatternToken::Kind::Level:
                *output << levels[static_cast<int>(entry.level)];
                break;
            case LogPatternToken::Kind::ElapsedTime: {
                auto d = entry.time - start_time_;
                *output
                    << std::chrono::duration_cast<std::chrono::milliseconds>(d)
                           .count()
                    << "ms";
                break;
            }
            case LogPatternToken::Kind::Message:
                *output << entry.message;
                break;
            }
        }

        *output << "\n";
    }

    const std::string name_;
    std::array<std::vector<LogPatternToken>, LogLevelCount> patterns_;
    std::array<std::ostream*, LogLevelCount> sinks_{};
    logger_clock::time_point start_time_ = logger_clock::now();
    std::uint8_t enabled_levels_{(1 << LogLevelCount) - 1};

    friend class LogStream;
};

class LogStream {
  public:
    LogStream(Logger& logger, LogLevel level)
        : logger_{logger}, level_{level}, time_{logger_clock::now()}, stream_{},
          logged_{false} {}

    ~LogStream() {
        LogEntry entry = {
            .level = level_, .time = time_, .message = stream_.str()};
        logger_.log(entry);
    }

    bool operator!() const { return !logged_ && logger_.is_enabled(level_); }

    template <typename T>
    LogStream& operator<<(const T& value) {
        stream_ << value;
        logged_ = true;
        return *this;
    }

  private:
    Logger& logger_;
    LogLevel level_;
    logger_clock::time_point time_;
    std::ostringstream stream_;
    bool logged_;
};

#define LOG(logger, level)                                                     \
    for (auto _log_stream = LogStream((logger), (level)); !_log_stream;)       \
    _log_stream

#define LOG_DEBUG(logger) LOG(logger, LogLevel::Debug)
#define LOG_INFO(logger) LOG(logger, LogLevel::Info)
#define LOG_WARN(logger) LOG(logger, LogLevel::Warn)
#define LOG_ERROR(logger) LOG(logger, LogLevel::Error)
