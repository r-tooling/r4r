#ifndef LOGGER_H
#define LOGGER_H

#include "common.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std::string_view_literals;
using namespace std::string_literals;

typedef std::chrono::steady_clock logger_clock;

#define LOG_LEVELS                                                             \
    X(Trace, "TRC")                                                            \
    X(Debug, "DBG")                                                            \
    X(Info, "INF")                                                             \
    X(Warn, "WRN")                                                             \
    X(Error, "ERR")

enum class LogLevel {
#define X(a, b) a,
    LOG_LEVELS
#undef X
};

constexpr size_t kLogLevels = static_cast<size_t>(LogLevel::Error) + 1;

struct LogEvent {
    std::string_view logger_name;
    LogLevel level;
    logger_clock::time_point timestamp;
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

    inline static std::array const keywords_ = {
#define X(a, b) #b##sv,
        LOG_PATTER_TOKEN_KINDS
#
#undef X
    };

    inline static std::unordered_map<std::string_view, std::string_view> const
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
            throw std::runtime_error(STR("Unknown keyword: " << kw));
        }
    }

    [[nodiscard]] bool eof() const { return pos == end; }

    char const* pos;
    char const* const end;
};

class LogFormatter {
  public:
    virtual ~LogFormatter() = default;
    virtual void format(LogEvent const& event, std::ostream& dst) = 0;
};

class PatternLogFormatter : public LogFormatter {
  public:
    explicit PatternLogFormatter(std::string_view const pattern)
        : pattern_{LogPatternParser(pattern).parse()} {}

    void format(LogEvent const& event, std::ostream& dst) override;

  private:
    std::vector<LogPatternToken> pattern_;
};

class LogSink {
  public:
    virtual ~LogSink() = default;
    LogSink(LogSink&&) = default;
    LogSink() = default;
    virtual void sink(LogEvent const& event) = 0;
};

class OutputStreamSink : public LogSink {
  public:
    OutputStreamSink(OutputStreamSink&&) = default;
    OutputStreamSink(std::shared_ptr<LogFormatter> formatter, std::ostream& dst)
        : formatter_{formatter}, dst_{dst} {}

    void sink(LogEvent const& event) override {
        formatter_->format(event, dst_);
    };

  private:
    std::shared_ptr<LogFormatter> formatter_;
    std::ostream& dst_;
};

class Logger {
  public:
    [[nodiscard]] bool is_enabled(LogLevel level) const {
        std::uint8_t n = 1 << static_cast<int>(level);
        return (enabled_levels_ & n) == n;
    }

    [[nodiscard]] std::string_view name() const { return name_; }

  private:
    explicit Logger(std::string name) : name_(std::move(name)) {}

    void log(LogEvent const& event) {
        if (!is_enabled(event.level)) {
            return;
        }
        sinks_[static_cast<int>(event.level)]->sink(event);
    }

    void set_sink(LogLevel level, std::shared_ptr<LogSink> sink) {
        sinks_[static_cast<int>(level)] = sink;
    }

    void set_sink(std::shared_ptr<LogSink> sink) {
        for (size_t i = 0; i < kLogLevels; i++) {
            sinks_[i] = sink;
        }
    }

    std::string const name_;
    std::array<std::shared_ptr<LogSink>, kLogLevels> sinks_{};
    std::uint8_t enabled_levels_{(1 << kLogLevels) - 1};

    friend class LogStream;
    friend class LogManager;
};

class LogStream {
  public:
    LogStream(Logger& logger, LogLevel level)
        : logger_{logger}, level_{level}, timestamp_{logger_clock::now()},
          stream_{}, logged_{false} {}

    ~LogStream() {
        LogEvent entry = {.logger_name = logger_.name(),
                          .level = level_,
                          .timestamp = timestamp_,
                          .message = stream_.str()};
        logger_.log(entry);
    }

    bool operator!() const { return !logged_ && logger_.is_enabled(level_); }

    template <typename T>
    LogStream& operator<<(T const& value) {
        stream_ << value;
        logged_ = true;
        return *this;
    }

  private:
    Logger& logger_;
    LogLevel level_;
    logger_clock::time_point timestamp_;
    std::ostringstream stream_;
    bool logged_;
};

#define LOG(logger, level)                                                     \
    for (auto _log_stream = LogStream((logger), (level)); !_log_stream;)       \
    _log_stream

#define LOG_TRACE(logger) LOG(logger, LogLevel::Trace)
#define LOG_DEBUG(logger) LOG(logger, LogLevel::Debug)
#define LOG_INFO(logger) LOG(logger, LogLevel::Info)
#define LOG_WARN(logger) LOG(logger, LogLevel::Warn)
#define LOG_ERROR(logger) LOG(logger, LogLevel::Error)

class LogManager {
  public:
    static LogManager& instance() {
        static LogManager instance;
        return instance;
    }
    static Logger logger(std::string const& name) {
        return instance().get_or_create_logger(name);
    }

    void enable(std::string const& name, LogLevel level) {
        get_or_create_logger(name).enabled_levels_ |=
            1 << static_cast<int>(level);
    }

    void disable(std::string const& name, LogLevel level) {
        get_or_create_logger(name).enabled_levels_ ^=
            1 << static_cast<int>(level);
    }

    void set_sink(std::string const& name, LogLevel level,
                  std::shared_ptr<LogSink> sink) {
        get_or_create_logger(name).sinks_[static_cast<int>(level)] = sink;
    }

    static std::chrono::steady_clock::time_point logger_start() {
        return logger_start_;
    }

  private:
    LogManager() { configure_root_logger(); }

    void configure_root_logger();

    Logger& get_or_create_logger(std::string const& name);

    void configure_logger(Logger& logger);

    Logger& parent_logger(Logger& logger);

    // the root logger has empty name
    static inline std::string const kRootLoggerName = "";
    static inline std::chrono::steady_clock::time_point const logger_start_ =
        std::chrono::steady_clock::now();

    std::unordered_map<std::string, Logger> loggers_;
};

inline Logger& LogManager::get_or_create_logger(std::string const& name) {
    if (auto it = loggers_.find(name); it != loggers_.end()) {
        return it->second;
    }

    auto new_logger = Logger{name};
    configure_logger(new_logger);
    auto it = loggers_.emplace(name, std::move(new_logger));
    return it.first->second;
}

inline void LogManager::configure_logger(Logger& logger) {
    auto parent = parent_logger(logger);
    logger.sinks_ = parent.sinks_;
}

inline Logger& LogManager::parent_logger(Logger& logger) {
    std::string name = std::string{logger.name()};
    auto pos = name.rfind('.');

    while (pos != std::string::npos) {
        auto parent_name = name.substr(0, pos);
        auto it = loggers_.find(parent_name);
        if (it != loggers_.end()) {
            return it->second;
        }
        pos = parent_name.rfind('.');
    }

    return loggers_.at(kRootLoggerName);
}

inline void LogManager::configure_root_logger() {
    assert(!loggers_.contains(kRootLoggerName));

    auto log = Logger(kRootLoggerName);
    auto sink = std::make_shared<OutputStreamSink>(
        std::make_shared<PatternLogFormatter>("[{level}] {logger}: {message}"),
        std::cout);
    log.set_sink(sink);
    loggers_.emplace(kRootLoggerName, log);
}

inline void PatternLogFormatter::format(LogEvent const& event,
                                        std::ostream& dst) {
    static std::array const levels = {
#define X(a, b) b,
        LOG_LEVELS
#undef X
    };

    for (auto const& token : pattern_) {
        switch (token.kind()) {
        case LogPatternToken::Kind::Text:
        case LogPatternToken::Kind::Color:
            dst << token.payload();
            break;
        case LogPatternToken::Kind::Logger:
            dst << event.logger_name;
            break;
        case LogPatternToken::Kind::Level:
            dst << levels[static_cast<int>(event.level)];
            break;
        case LogPatternToken::Kind::ElapsedTime: {
            auto d = event.timestamp - LogManager::logger_start();
            dst << std::chrono::duration_cast<std::chrono::milliseconds>(d)
                       .count()
                << "ms";
            break;
        }
        case LogPatternToken::Kind::Message:
            dst << event.message;
            break;
        }
    }

    dst << "\n";
}

#endif // LOGGER_H
