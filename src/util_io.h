#ifndef UTIL_IO_H
#define UTIL_IO_H

#include "common.h"
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <streambuf>
#include <unistd.h>
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
    explicit LinePrefixingFilter(std::string prefix)
        : prefix_{std::move(prefix)} {}

    void operator()(std::streambuf& dst, int c) {
        if (at_nl) {
            dst.sputn(prefix_.data(),
                      static_cast<std::streamsize>(prefix_.size()));
            at_nl = false;
        }
        dst.sputc(static_cast<char>(c));
        if (c == '\n') {
            at_nl = true;
        }
    }

  private:
    std::string prefix_;
    bool at_nl{true};
};

template <typename Fn>
inline void with_prefixed_ostream(std::ostream& dst, std::string const& prefix,
                                  Fn fn) {
    auto* old = dst.rdbuf();
    FilteringOutputStreamBuf prefix_buf(dst.rdbuf(),
                                        LinePrefixingFilter{prefix});
    dst.rdbuf(&prefix_buf);
    fn();
    dst.flush();
    dst.rdbuf(old);
}

#include <poll.h>

inline void forward_output(int fd, std::ostream& out) {
    constexpr std::size_t kBufferSize = 4 * 1024ULL;
    std::array<char, kBufferSize> buffer{};

    pollfd pfd{fd, POLLIN, 0};

    while (true) {
        int ret = ::poll(&pfd, 1, -1);
        if (ret == -1) {
            if (errno == EINTR) { // interrupted by signal, try again
                continue;
            }
            throw std::runtime_error(std::string("poll: ") +
                                     std::strerror(errno));
        }

        if (pfd.revents & POLLIN) {
            ssize_t n = ::read(fd, buffer.data(), buffer.size());
            if (n == 0) { // EOF
                break;
            }
            if (n < 0) {
                if (errno == EINTR) { // interrupted mid-read
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue; // shouldn’t happen because poll said it’s ready
                }
                throw make_system_error(
                    errno, STR("Unable to read from file descriptor: " << fd));
            }

            out.write(buffer.data(), static_cast<std::streamsize>(n));
            if (!out) { // disk full, pipe closed, ...
                throw make_system_error(
                    errno, STR("Unable to write to stream: " << fd));
            }
        }

        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            break; // peer hung up or fd became invalid
        }
    }

    out.flush();
}

#endif // UTIL_IO_H
