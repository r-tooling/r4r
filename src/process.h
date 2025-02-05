#ifndef PROCESS_H
#define PROCESS_H

#include "common.h"
#include "logger.h"
#include "util.h"
#include <optional>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

class Pipe {
  public:
    Pipe(Pipe const&) = delete;
    Pipe& operator=(Pipe const&) = delete;

    Pipe() {
        int fds[2];
        if (::pipe(fds) < 0) {
            throw make_system_error(errno, "Failed to create pipe");
        }
        read_fd = fds[0];
        write_fd = fds[1];
    }
    Pipe(Pipe&& other) noexcept
        : read_fd(other.read_fd), write_fd(other.write_fd) {
        other.read_fd = -1;
        other.write_fd = -1;
    }

    Pipe& operator=(Pipe&& other) noexcept {
        if (this != &other) {
            close();
            read_fd = other.read_fd;
            write_fd = other.write_fd;
            other.read_fd = -1;
            other.write_fd = -1;
        }
        return *this;
    }

    ~Pipe() { close(); }

    [[nodiscard]] int read() const { return read_fd; }

    [[nodiscard]] int write() const { return write_fd; }

    void close_read() {
        if (read_fd != -1) {
            ::close(read_fd);
            read_fd = -1;
        }
    }

    void close_write() {
        if (write_fd != -1) {
            ::close(write_fd);
            write_fd = -1;
        }
    }

    void close() {
        close_read();
        close_write();
    }

  private:
    int read_fd{-1};
    int write_fd{-1};
};

class Output {
  public:
    std::string stdout_data;
    std::string stderr_data;
    int exit_code{0};

    void check_success(std::string const& message) const;
};

inline void Output::check_success(std::string const& message) const {
    if (exit_code) {
        throw std::runtime_error(STR(message << " (exit code: " << exit_code
                                             << ")\nstderr:\n"
                                             << stderr_data));
    }
}

class Child {
  public:
    Child(pid_t pid, Pipe stdout_pipe, Pipe stderr_pipe)
        : pid_(pid), stdout_(std::move(stdout_pipe)),
          stderr_(std::move(stderr_pipe)) {}

    [[nodiscard]] int wait() const {
        int status = 0;
        pid_t result = ::waitpid(pid_, &status, 0);

        if (result < 0) {
            throw make_system_error(errno, STR("waitpid failed for " << pid_));
        }

        return status_to_exit_code(status);
    }

    [[nodiscard]] std::optional<int> try_wait() const {
        int status;
        pid_t result = waitpid(pid_, &status, WNOHANG);
        if (result == 0) {
            return {};
        } else if (result == pid_) {
            return status_to_exit_code(status);
        } else {
            throw make_system_error(errno, STR("waitpid failed for " << pid_));
        }
    }

    void kill(int signal = SIGKILL) const {
        if (pid_ > 0) {
            if (::kill(pid_, signal) < 0 && errno != ESRCH) {
                throw make_system_error(errno, "Failed to kill process");
            }
        }
    }

    std::string read_stdout() {
        if (!stdout_.read()) {
            return "";
        }
        std::string data = read_all_from_fd(stdout_.read());
        stdout_.close_read();
        return data;
    }

    std::string read_stderr() {
        if (!stderr_.read()) {
            return "";
        }
        std::string data = read_all_from_fd(stderr_.read());
        stderr_.close_read();
        return data;
    }

    [[nodiscard]] pid_t pid() const { return pid_; }

  private:
    pid_t pid_{-1};
    Pipe stdout_;
    Pipe stderr_;

    static int status_to_exit_code(int status) {
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            return 126 + WTERMSIG(status);
        } else {
            return status;
        }
    }

    static std::string read_all_from_fd(int fd) {
        constexpr size_t buf_size = 4096;
        char buffer[buf_size];
        std::string result;

        while (true) {
            ssize_t bytes_read = ::read(fd, buffer, buf_size);
            if (bytes_read < 0) {
                // retry if interrupted by signal; otherwise throw.
                if (errno == EINTR) {
                    continue;
                }

                throw make_system_error(
                    errno, STR("Failed to read from pipe: " << fd));
            } else if (bytes_read == 0) {
                // EOF
                break;
            } else {
                result.append(buffer, static_cast<size_t>(bytes_read));
            }
        }
        return result;
    }
};

enum class Stdio {
    Inherit, // inherit from parent
    Pipe,    // redirect to a pipe (to capture or read/write)
    Merge // merge this stream with the other (stderr->stdout or stdout->stderr)
};

class Command {
  public:
    explicit Command(std::string const& program);

    Command& arg(std::string const& arg);

    Command& args(std::vector<std::string> const& args);

    Command& env(std::string const& key, std::string const& value);

    Command& current_dir(std::string const& dir);

    Command& set_stdout(Stdio stdio);

    Command& set_stderr(Stdio stdio);

    Child spawn();

    Output output(bool redirect_stderr_to_stdout = false);

  private:
    static inline Logger& log_ = LogManager::logger("command");
    std::vector<std::string> args_;
    std::map<std::string, std::string> envs_;
    std::optional<std::string> working_dir_;
    std::optional<Stdio> stdout_setting_{};
    std::optional<Stdio> stderr_setting_{};
};

inline Command::Command(std::string const& program) {
    args_.push_back(program);
}

inline Command& Command::arg(std::string const& arg) {
    args_.push_back(arg);
    return *this;
}

inline Command& Command::args(std::vector<std::string> const& args) {
    for (auto& a : args) {
        args_.push_back(a);
    }
    return *this;
}

inline Command& Command::env(std::string const& key, std::string const& value) {
    envs_[key] = value;
    return *this;
}

inline Command& Command::current_dir(std::string const& dir) {
    working_dir_ = dir;
    return *this;
}

inline Command& Command::set_stdout(Stdio stdio) {
    stdout_setting_ = stdio;
    return *this;
}

inline Command& Command::set_stderr(Stdio stdio) {
    stderr_setting_ = stdio;
    return *this;
}

inline Child Command::spawn() {
    if (!stdout_setting_) {
        set_stdout(Stdio::Inherit);
    }
    if (!stderr_setting_) {
        set_stderr(Stdio::Inherit);
    }

    Pipe out;
    Pipe err;

    pid_t pid = ::fork();
    if (pid < 0) {
        throw make_system_error(errno, "Failed to fork");
    }

    if (pid == 0) {

        if (working_dir_.has_value()) {
            if (::chdir(working_dir_->c_str()) < 0) {
                _exit(127);
            }
        }

        for (auto& [k, v] : envs_) {
            ::setenv(k.c_str(), v.c_str(), 1);
        }

        if (*stdout_setting_ == Stdio::Pipe) {
            ::dup2(out.write(), STDOUT_FILENO);
        } else if (*stdout_setting_ == Stdio::Merge) {
            // stdout -> stderr
            ::dup2(STDERR_FILENO, STDOUT_FILENO);
        }

        if (*stderr_setting_ == Stdio::Pipe) {
            ::dup2(err.write(), STDERR_FILENO);
        } else if (*stderr_setting_ == Stdio::Merge) {
            // stderr -> stdout
            ::dup2(STDOUT_FILENO, STDERR_FILENO);
        }

        out.close();
        err.close();

        LOG_TRACE(log_) << "Running a command " << string_join(args_, ' ');

        std::vector<char*> argv = collection_to_c_array(args_);

        ::execvp(argv.front(), argv.data());
        _exit(127);
    }

    out.close_write();
    err.close_write();

    return {pid, std::move(out), std::move(err)};
}

inline Output Command::output(bool redirect_stderr_to_stdout) {
    if (!stdout_setting_) {
        set_stdout(Stdio::Pipe);
    }
    if (!stderr_setting_) {
        set_stderr(Stdio::Pipe);
    }
    if (redirect_stderr_to_stdout) {
        set_stderr(Stdio::Merge);
    }

    Child child = spawn();

    std::string out = child.read_stdout();
    std::string err = child.read_stderr();
    int exit_code = child.wait();

    return {std::move(out), std::move(err), exit_code};
}

#endif // PROCESS_H
