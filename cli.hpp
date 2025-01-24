#include <atomic>
#include <chrono>
#include <csignal>
#include <deque>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

class ThreadSafeStreamBuf : public std::streambuf {
  public:
    ThreadSafeStreamBuf() = default;

    std::string get_new_data() {
        std::lock_guard<std::mutex> lock(m_);
        if (read_pos_ >= buffer_.size()) {
            return "";
        }
        std::string new_data = buffer_.substr(read_pos_);
        read_pos_ = buffer_.size();
        return new_data;
    }

  protected:
    int overflow(int ch) override {
        if (ch == traits_type::eof()) {
            return ch;
        }
        {
            std::lock_guard<std::mutex> lock(m_);
            buffer_.push_back(static_cast<char>(ch));
        }
        return ch;
    }

    int sync() override {
        return 0; // success
    }

  private:
    std::mutex m_;
    // FIXME: do not use string
    std::string buffer_;
    size_t read_pos_ = 0;
};

class ThreadSafeOStream : public std::ostream {
  public:
    ThreadSafeOStream() : std::ostream(&buf_) {}
    std::string get_new_data() { return buf_.get_new_data(); }

  private:
    ThreadSafeStreamBuf buf_;
};

class ProgressBar {
  public:
    ProgressBar() = default;

    void set_task_name(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_);
        task_name_ = name;
    }

    void start() {
        std::lock_guard<std::mutex> lock(m_);
        start_time_ = std::chrono::steady_clock::now();
    }

    long elapsed_seconds() const {
        std::lock_guard<std::mutex> lock(m_);
        auto now = std::chrono::steady_clock::now();
        auto secs =
            std::chrono::duration_cast<std::chrono::seconds>(now - start_time_)
                .count();
        return secs;
    }

    void set_infinite_mode(bool enabled) {
        std::lock_guard<std::mutex> lock(m_);
        infinite_mode_ = enabled;
        if (infinite_mode_) {
            anim_index_ = 0;
            anim_dir_ = 1;
        }
    }

    bool is_infinite_mode() const {
        std::lock_guard<std::mutex> lock(m_);
        return infinite_mode_;
    }

    void set_progress(double p) {
        std::lock_guard<std::mutex> lock(m_);
        if (p < 0.0)
            p = 0.0;
        if (p > 1.0)
            p = 1.0;
        progress_ = p;
    }

    double get_progress() const {
        std::lock_guard<std::mutex> lock(m_);
        return progress_;
    }

    std::string build_bar_string(int width) {
        std::lock_guard<std::mutex> lock(m_);

        auto now = std::chrono::steady_clock::now();
        long secs =
            std::chrono::duration_cast<std::chrono::seconds>(now - start_time_)
                .count();

        // reset formatting before printing
        if (!infinite_mode_) {
            int filled = static_cast<int>(progress_ * width + 0.5);
            if (filled > width)
                filled = width;
            std::string tmp(filled, '#');
            tmp.append(width - filled, ' ');
            std::string bar = "[" + tmp + "]";
            int pct = static_cast<int>(progress_ * 100.0 + 0.5);
            return "\x1B[0m[" + task_name_ + "] " + bar + " " +
                   std::to_string(pct) + "%" + " (" + std::to_string(secs) +
                   "s)";
        } else {
            std::string tmp(width, ' ');
            const std::string marker = "<=>";
            int m_len = (int)marker.size();
            if (anim_index_ < 0)
                anim_index_ = 0;
            if (anim_index_ > width - m_len)
                anim_index_ = width - m_len;

            for (int i = 0; i < m_len; i++) {
                tmp[anim_index_ + i] = marker[i];
            }
            anim_index_ += anim_dir_;
            if (anim_index_ <= 0) {
                anim_index_ = 0;
                anim_dir_ = 1;
            } else if (anim_index_ >= width - m_len) {
                anim_index_ = width - m_len;
                anim_dir_ = -1;
            }
            std::string bar = "[" + tmp + "]";
            return "\x1B[0m[" + task_name_ + "] " + bar + " (" +
                   std::to_string(secs) + "s)";
        }
    }

  private:
    mutable std::mutex m_;
    std::string task_name_;
    std::chrono::steady_clock::time_point start_time_ =
        std::chrono::steady_clock::now();

    bool infinite_mode_ = false;
    double progress_ = 0.0;

    int anim_index_ = 0;
    int anim_dir_ = 1;
};

class Task {
  public:
    explicit Task(std::string name) : name_(std::move(name)) {}

    virtual ~Task() = default;

    std::string const& get_name() const { return name_; }

    virtual void stop() {}

    virtual bool run_task(std::ostream& output, ProgressBar& bar) = 0;

  private:
    std::string name_;
};

class TaskManager {
  public:
    TaskManager(int n_lines, bool hide_cursor = true)
        : n_lines_(n_lines), hide_cursor_(hide_cursor) {
        std::signal(SIGINT, &TaskManager::signal_handler);

        // [NEW] We use terminfo to see if we have cursor-up, clear-line, etc.
        ansi_supported_ = check_ansi_support();
    }

    ~TaskManager() {
        if (hide_cursor_ && ansi_supported_) {
            std::cout << "\x1B[?25h" << std::flush;
        }
    }

    void run_task(Task& t) {
        // if (!ansi_supported_) {
        //     // [CHANGED] Non-ANSI path: we track start, so we can print
        //     elapsed bar_.set_task_name(t.get_name()); bar_.start(); std::cout
        //     << "Task " << t.get_name() << " started.\n";
        //
        //     auto sink = std::make_shared<TsoLoggerSink>(ts_out_);
        //     auto logger = std::make_shared<Logger>(t.get_name(), sink);
        //
        //     worker_done_ = false;
        //     result_ = false;
        //     current_task_ = &t;
        //
        //     worker_thread_ = std::thread([&]() {
        //         bool r = t.run_task(logger, bar_);
        //         result_ = r;
        //         worker_done_ = true;
        //     });
        //
        //     while (true) {
        //         if (interrupted_.load()) {
        //             stop_current_task();
        //             break;
        //         }
        //         std::string data = ts_out_.get_new_data();
        //         if (!data.empty()) {
        //             std::cout << data << std::flush;
        //         }
        //         if (worker_done_.load()) {
        //             break;
        //         }
        //         std::this_thread::sleep_for(std::chrono::milliseconds(100));
        //     }
        //
        //     if (worker_thread_.joinable()) {
        //         worker_thread_.join();
        //     }
        //     current_task_ = nullptr;
        //
        //     // [NEW] print elapsed time from bar
        //     long secs = bar_.elapsed_seconds();
        //     std::cout << "Task " << t.get_name() << " ended in " << secs
        //               << "s.\n";
        //     return;
        // }

        bar_.set_task_name(t.get_name());
        bar_.start();

        worker_done_ = false;
        result_ = false;
        current_task_ = &t;
        line_buffer_.clear();
        partial_.clear();
        displayed_line_count_ = 0;

        if (hide_cursor_) {
            std::cout << "\x1B[?25l";
        }

        worker_thread_ = std::thread([&]() {
            bool r = t.run_task(ts_out_, bar_);
            result_ = r;
            worker_done_ = true;
        });

        terminal_width_ = get_terminal_width();
        if (terminal_width_ <= 0) {
            terminal_width_ = 80;
        }

        auto last_refresh = std::chrono::steady_clock::now();
        const int REFRESH_MS = 100;

        while (true) {
            process_new_data();

            // FIXME: this won't work
            // if (interrupted_.load()) {
            //     stop_current_task();
            //     break;
            // }

            auto now = std::chrono::steady_clock::now();
            bool time_for_update =
                (std::chrono::duration_cast<std::chrono::milliseconds>(
                     now - last_refresh)
                     .count() >= REFRESH_MS);

            bool done = worker_done_.load();
            if (time_for_update || done) {
                update_screen(/*final=*/done);
                last_refresh = now;
            }

            if (done) {
                if (result_) {
                    remove_output_lines();
                }
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }

        current_task_ = nullptr;
    }

    void stop() {
        if (current_task_) {
            stop_current_task();
        }
    }

  private:
    // [NEW] use terminfo to check if "cuu1" and "el" exist
    static bool check_ansi_support() {
        // must be a tty
        if (!isatty(STDOUT_FILENO)) {
            return false;
        }
        // FIXME: better check for the ANSI commands
        return true;
    }

    static void signal_handler(int signo) {
        if (signo == SIGINT) {
            // interrupted_.store(true);
        }
    }

    void stop_current_task() {
        if (current_task_) {
            current_task_->stop();
        }
    }

    void process_new_data() {
        std::string new_data = ts_out_.get_new_data();
        if (new_data.empty())
            return;
        partial_ += new_data;
        size_t pos = 0;
        while (true) {
            auto nl = partial_.find('\n', pos);
            if (nl == std::string::npos)
                break;
            auto line = partial_.substr(pos, nl - pos);
            pos = nl + 1;
            store_line(line);
        }
        partial_.erase(0, pos);
    }

    void store_line(const std::string& line) {
        size_t w = static_cast<size_t>(terminal_width_);
        size_t start = 0;
        if (line.empty()) {
            if ((int)line_buffer_.size() == n_lines_) {
                line_buffer_.pop_front();
            }
            line_buffer_.push_back("");
            return;
        }
        while (start < line.size()) {
            size_t chunk_len = std::min(w, line.size() - start);
            std::string chunk = line.substr(start, chunk_len);
            if ((int)line_buffer_.size() == n_lines_) {
                line_buffer_.pop_front();
            }
            line_buffer_.push_back(chunk);
            start += chunk_len;
        }
    }

    void update_screen(bool final) {
        clear_previous_lines();

        std::string bar_str = bar_.build_bar_string(20);
        clear_line();
        std::cout << bar_str << "\n";

        if (!final) {
            for (auto& ln : line_buffer_) {
                clear_line();
                std::cout << ln << "\n";
            }
            displayed_line_count_ = static_cast<int>(line_buffer_.size()) + 1;
        } else {
            for (auto& ln : line_buffer_) {
                clear_line();
                std::cout << ln << "\n";
            }
            displayed_line_count_ = static_cast<int>(line_buffer_.size()) + 1;
        }
        std::fflush(stdout);
    }

    void remove_output_lines() {
        clear_previous_lines();
        std::string bar_str = bar_.build_bar_string(20);
        clear_line();
        std::cout << bar_str << "\n";
        displayed_line_count_ = 1;
        std::fflush(stdout);
    }

    void clear_previous_lines() {
        move_cursor_up(displayed_line_count_);
        for (int i = 0; i < displayed_line_count_; i++) {
            clear_line();
            std::cout << "\n";
        }
        if (displayed_line_count_ > 0) {
            move_cursor_up(displayed_line_count_);
        }
    }

    static void move_cursor_up(int lines) {
        if (lines > 0) {
            std::cout << "\x1B[" << lines << "A";
        }
    }

    static void clear_line() { std::cout << "\x1B[2K"; }

    static int get_terminal_width() {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
            if (ws.ws_col > 0) {
                return ws.ws_col;
            }
        }
        return 80;
    }

  private:
    ThreadSafeOStream ts_out_;
    ProgressBar bar_;

    std::thread worker_thread_;
    std::atomic<bool> worker_done_{false};
    bool result_ = false;
    Task* current_task_{nullptr};

    int terminal_width_ = 80;

    bool ansi_supported_{true};

    std::deque<std::string> line_buffer_;
    std::string partial_;
    int displayed_line_count_ = 0;
    int n_lines_ = 5;
    bool hide_cursor_ = true;
};
