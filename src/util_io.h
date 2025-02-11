#ifndef UTIL_IO_H
#define UTIL_IO_H

#include <array>
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
        : prefix_{std::move(prefix)}, at_nl{true} {}

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

inline void forward_output(int read_fd, std::ostream& os, char const* tag) {
    constexpr size_t BUFFER_SIZE = 1024;
    std::array<char, BUFFER_SIZE> buffer{};
    while (true) {
        ssize_t bytes = ::read(read_fd, buffer.data(), buffer.size());
        if (bytes == 0) {
            break; // EOF
        }
        if (bytes < 0) {
            if (errno == EINTR) {
                continue; // retry
            }
            std::cerr << "[Tracer] " << tag
                      << " read error: " << strerror(errno) << '\n';
            break;
        }
        os.write(buffer.data(), bytes);
        os.flush();
    }
}

#endif // UTIL_IO_H
