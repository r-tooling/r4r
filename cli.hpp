#include "common.hpp"
#include "logger.hpp"
#include "util.hpp"

#include <chrono>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <streambuf>
#include <string>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

template <typename Filter>
class FilteringOutputStreamBuf : public std::streambuf {
  public:
    FilteringOutputStreamBuf(std::streambuf* dest, Filter filter)
        : target_(dest), filter_(std::move(filter)) {}

  protected:
    int overflow(int ch) override {
        if (ch == traits_type::eof()) {
            return traits_type::not_eof(ch);
        }
        filter_(*target_, ch);
        return ch;
    }

    int sync() override { return target_->pubsync(); }

  private:
    std::streambuf* target_;
    Filter filter_;
};

class LinePrefixingFilter {
  public:
    LinePrefixingFilter(std::string prefix) : prefix_{prefix}, at_nl{true} {}
    void operator()(std::streambuf& dst, int c) {
        if (at_nl) {
            dst.sputn(prefix_.data(), prefix_.size());
            at_nl = false;
        }
        dst.sputc(static_cast<char>(c));
        if (c == '\n') {
            at_nl = true;
        }
    }

  private:
    std::string prefix_;
    bool at_nl;
};

class TaskBase {
  public:
    TaskBase(std::string name) : name_{std::move(name)} {}
    virtual ~TaskBase() = default;
    virtual void stop() {}
    std::string const& name() { return name_; }

  private:
    std::string name_;
};

template <typename T>
class Task : public TaskBase {
  public:
    Task(std::string name) : TaskBase{std::move(name)} {}
    virtual T run(Logger& log, std::ostream& output) = 0;
};

class TaskRunner {
  public:
    explicit TaskRunner(std::ostream& output)
        : output_(output), log_(Logger{"task-runner", output_}) {
        log_.set_pattern("[{level}] {logger}: {message}");
        log_.set_pattern(LogLevel::Warn, "[{logger}]: {message}");
        log_.set_sink(LogLevel::Warn, warnings_);
        log_.set_sink(LogLevel::Error, std::cerr);
    }

    template <typename T>
    T run(Task<T>&& task) {
        auto& ref = task;
        return run(ref);
    }

    template <typename T>
    T run(Task<T>& task) {
        current_task_ = &task;

        auto before = std::chrono::steady_clock::now();
        LOG_INFO(log_) << task.name() << " starting";
        Logger task_log{task.name(), log_};

        T result = task.run(task_log, output_);
        output_.flush();

        auto now = std::chrono::steady_clock::now();
        auto elapsed = now - before;
        LOG_INFO(log_) << task.name() << " finished in "
                       << util::format_elapsed_time(elapsed);

        current_task_ = nullptr;

        return result;
    }

    void stop() {
        if (current_task_) {
            current_task_->stop();
        }
    }

  private:
    std::ostream& output_;
    Logger log_;
    std::ostringstream warnings_;
    TaskBase* current_task_;
};

template <typename Fn>
void prefixed_ostream(std::ostream& dst, std::string prefix, Fn fn) {
    auto old = dst.rdbuf();
    FilteringOutputStreamBuf prefix_buf(old, LinePrefixingFilter{prefix});
    dst.rdbuf(&prefix_buf);
    fn();
    dst.flush();
    dst.rdbuf(old);
}
