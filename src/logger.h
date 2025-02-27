#ifndef LOGGER_H
#define LOGGER_H

#include "common.h"
#include <array>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

enum class LogLevel { Trace, Debug, Info, Warning, Error, Fatal };

inline LogLevel& operator++(LogLevel& level) {
    if (level < LogLevel::Fatal) {
        level = static_cast<LogLevel>(static_cast<int>(level) + 1);
    }
    return level;
}

inline LogLevel& operator--(LogLevel& level) {
    if (level > LogLevel::Trace) {
        level = static_cast<LogLevel>(static_cast<int>(level) - 1);
    }
    return level;
}

struct LogEvent {
    LogLevel level;
    std::string_view message;
    std::string_view filename;
    int line;
};

namespace std {
inline std::ostream& operator<<(std::ostream& os, LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
        os << "TRACE";
        break;
    case LogLevel::Debug:
        os << "DEBUG";
        break;
    case LogLevel::Info:
        os << "INFO";
        break;
    case LogLevel::Warning:
        os << "WARN";
        break;
    case LogLevel::Error:
        os << "ERROR";
        break;
    case LogLevel::Fatal:
        os << "FATAL";
        break;
    };
    return os;
}
} // namespace std

class LogSink {
  public:
    virtual ~LogSink() = default;
    virtual void sink(LogEvent const& event) = 0;
    virtual void sync() = 0;
};

class ConsoleSink : public LogSink {
  public:
    void sink(LogEvent const& event) override {
        std::ostream& os =
            (event.level >= LogLevel::Warning) ? std::cerr : std::cout;

        auto msg = STR("[" << std::setw(5) << std::right << event.level << "] "
                           << " " << event.message << "\n");
        os << msg;
    }

    void sync() override {
        std::cout << std::flush;
        std::cerr << std::flush;
    }
};

class StoreSink : public LogSink {
  public:
    struct StoredEvent {
        LogLevel level;
        std::string message;
        std::string filename;
        int line;

        explicit StoredEvent(LogEvent const& ev)
            : level(ev.level), message(ev.message), filename(ev.filename),
              line(ev.line) {}

        [[nodiscard]] LogEvent to_log_event() const {
            return {.level = level,
                    .message = message,
                    .filename = filename,
                    .line = line};
        }
    };

    void sink(LogEvent const& event) override { messages_.emplace_back(event); }

    [[nodiscard]] std::vector<StoredEvent> get_messages() const {
        return messages_;
    }

    void sync() override {}

  private:
    std::vector<StoredEvent> messages_;
};

class Logger {
  public:
    static Logger& get() {
        static Logger instance;
        return instance;
    }

    void enable(LogLevel level) { set_level(level, true); }
    void disable(LogLevel level) { set_level(level, false); }
    void set_max_level(LogLevel max_level);
    [[nodiscard]] bool is_enabled(LogLevel level) const;

    std::unique_ptr<LogSink> set_sink(std::unique_ptr<LogSink> sink);
    [[nodiscard]] LogSink& get_sink() const { return *sink_; }
    template <typename Sink>
    [[nodiscard]] std::unique_ptr<Sink>
    with_sink(std::unique_ptr<Sink> sink, std::function<void()> const& thunk);

    void log(LogEvent const& event);

  private:
    Logger() {
        set_sink(std::make_unique<ConsoleSink>());
        set_max_level(LogLevel::Info);
    }

    void set_level(LogLevel level, bool enabled);

    static size_t constexpr kLevelsCount =
        static_cast<size_t>(LogLevel::Fatal) + 1;

    std::unique_ptr<LogSink> sink_;
    std::array<bool, kLevelsCount> levels_enabled_{};
    std::mutex mutex_;
};

inline void Logger::set_max_level(LogLevel max_level) {
    for (size_t i = 0; i < kLevelsCount; ++i) {
        set_level(static_cast<LogLevel>(i),
                  i >= static_cast<size_t>(max_level));
    }
}

inline bool Logger::is_enabled(LogLevel level) const {
    if (level == LogLevel::Fatal) {
        return true;
    }

    return levels_enabled_.at(static_cast<size_t>(level));
}

inline std::unique_ptr<LogSink>
Logger::set_sink(std::unique_ptr<LogSink> sink) {
    sink_.swap(sink);
    return sink;
}

template <typename Sink>
std::unique_ptr<Sink> Logger::with_sink(std::unique_ptr<Sink> sink,
                                        std::function<void()> const& thunk) {
    auto old_sink = set_sink(std::move(sink));

    thunk();

    auto sink_raw = set_sink(std::move(old_sink)).release();
    auto sink_raw_cast = dynamic_cast<Sink*>(sink_raw);
    return std::unique_ptr<Sink>(sink_raw_cast);
}

inline void Logger::log(LogEvent const& event) {
    std::lock_guard lock(mutex_);
    if (!is_enabled(event.level)) {
        return;
    }

    if (sink_) {
        sink_->sink(event);
    }

    if (event.level == LogLevel::Fatal) {
        sink_->sync();
        std::abort();
    }
}

inline void Logger::set_level(LogLevel level, bool enabled) {
    if (level == LogLevel::Fatal) {
        return;
    }

    std::lock_guard lock(mutex_);
    levels_enabled_.at(static_cast<size_t>(level)) = enabled;
}

class LogMessage {
  public:
    LogMessage(LogLevel level, char const* filename, int line)
        : level_(level), filename_(filename), line_(line) {}

    ~LogMessage() {
        Logger::get().log({.level = level_,
                           .message = stream_.str(),
                           .filename = filename_,
                           .line = line_});
    }

    std::ostream& stream() { return stream_; }

  private:
    LogLevel level_;
    char const* filename_;
    int line_;
    std::ostringstream stream_;
};

#define TRACE LogLevel::Trace
#define DEBUG LogLevel::Debug
#define INFO LogLevel::Info
#define WARN LogLevel::Warning
#define ERROR LogLevel::Error
#define FATAL LogLevel::Fatal

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LOG(level)                                                             \
    for (bool enabled = Logger::get().is_enabled(level); enabled;              \
         enabled = false)                                                      \
    LogMessage(level, __FILE__, __LINE__).stream()

#define CHECK(condition)                                                       \
    if (!(condition))                                                          \
    LogMessage(LogLevel::Fatal, __FILE__, __LINE__).stream()                   \
        << "Check failed: " #condition " "

#endif // LOGGER_H
