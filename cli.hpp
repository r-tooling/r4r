#include "common.hpp"

#include <fcntl.h>
#include <iostream>
#include <poll.h>
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
    virtual ~TaskBase() = default;
    virtual void stop() {}
};

template <typename T>
class Task : public TaskBase {
  public:
    virtual T run(std::ostream& output) = 0;
};

class TaskRunner {
  public:
    explicit TaskRunner(std::ostream& output) : output_(output) {}

    template <typename T>
    T run(Task<T>& task) {
        current_task_ = &task;
        T result = task.run(output_);
        output_.flush();

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
