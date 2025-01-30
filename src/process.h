#ifndef PROCESS_H
#define PROCESS_H

#include "common.h"
#include "util.h"
#include <array>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <variant>
#include <vector>

class ProcessStreambuf : public std::streambuf {
  private:
    int pipe_fd_;
    std::array<char, 4096> buffer_{};

  public:
    explicit ProcessStreambuf(int pipe_fd) : pipe_fd_(pipe_fd) {
        setg(buffer_.data(), buffer_.data(), buffer_.data());
    }

    ~ProcessStreambuf() override {
        if (pipe_fd_ != -1) {
            close(pipe_fd_);
        }
    }

  protected:
    int underflow() override {
        if (pipe_fd_ == -1)
            return traits_type::eof();

        ssize_t bytes_read = read(pipe_fd_, buffer_.data(), buffer_.size());
        if (bytes_read <= 0)
            return traits_type::eof();

        setg(buffer_.data(), buffer_.data(), buffer_.data() + bytes_read);
        return traits_type::to_int_type(*gptr());
    }
};

class Process {
  private:
    pid_t pid_;
    int exit_code_;
    bool exited_;
    int pipe_fd_;
    std::unique_ptr<ProcessStreambuf> buffer_;
    std::istream stream_;

  public:
    explicit Process(std::vector<std::string> const& cmd,
                     bool combine_streams = false)
        : pid_(-1), exit_code_(-1), exited_(false), pipe_fd_(-1),
          buffer_(nullptr), stream_(nullptr) {
        start_process(cmd, combine_streams);
    }

    ~Process() {
        if (pipe_fd_ != -1) {
            close(pipe_fd_);
        }
        if (pid_ > 0) {
            waitpid(pid_, nullptr, 0);
        }
    }

    std::istream& output() { return stream_; }

    bool is_running() {
        if (exited_)
            return false;
        int status;
        pid_t result = waitpid(pid_, &status, WNOHANG);
        if (result == 0)
            return true;
        if (result == pid_) {
            exited_ = true;
            exit_code_ = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            return false;
        }
        return false;
    }

    int exit_code() {
        if (!exited_)
            is_running();
        return exit_code_;
    }

    int wait() {
        if (!exited_) {
            int status;
            pid_t result = waitpid(pid_, &status, 0);

            if (result == pid_) {
                if (WIFEXITED(status)) {
                    exit_code_ = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    exit_code_ = 128 + WTERMSIG(status);
                } else {
                    throw std::runtime_error(
                        "Unknown reason for the process termination");
                }
                exited_ = true;
            } else {
                throw make_system_error(
                    errno, STR("waitpid for: " << pid_ << " failed"));
            }
        }
        return exit_code_;
    }

  private:
    void start_process(std::vector<std::string> const& cmd,
                       bool combine_output) {
        int pipe_fds[2];
        if (pipe(pipe_fds) == -1) {
            throw std::runtime_error("pipe() failed");
        }

        pid_ = fork();
        if (pid_ == -1) {
            close(pipe_fds[0]);
            close(pipe_fds[1]);
            throw std::runtime_error("fork() failed");
        }

        if (pid_ == 0) {
            // child process
            close(pipe_fds[0]);
            dup2(pipe_fds[1], STDOUT_FILENO);
            if (combine_output) {
                dup2(pipe_fds[1], STDERR_FILENO);
            }
            close(pipe_fds[1]);

            auto c_args = collection_to_c_array(cmd);
            execvp(c_args[0], c_args.get());

            perror("execvp");
            exit(1);
        } else {
            // parent process
            close(pipe_fds[1]);
            pipe_fd_ = pipe_fds[0];

            buffer_ = std::make_unique<ProcessStreambuf>(pipe_fd_);
            stream_.rdbuf(buffer_.get());
        }
    }
};

inline std::pair<std::string, int>
execute_command(std::vector<std::string> const& cmd,
                bool combine_output = false) {
    Process proc(cmd, combine_output);
    std::ostringstream result;
    std::string line;

    while (std::getline(proc.output(), line)) {
        result << line << '\n';
    }

    int exit_code = proc.wait();
    return {result.str(), exit_code};
}

inline void execute_command(
    std::vector<std::string> const& cmd,
    std::function<void(std::variant<std::string, int>)> const& callback,
    bool combine_output = false) {
    Process proc(cmd, combine_output);
    std::string line;

    while (std::getline(proc.output(), line)) {
        callback(line);
    }

    int exit_code = proc.wait();

    callback(exit_code);
}

#endif // PROCESS_H
