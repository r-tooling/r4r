#ifndef UTIL_IO_H
#define UTIL_IO_H

#include <iostream>
#include <streambuf>
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
    bool at_nl;
};

template <typename Fn>
void prefixed_ostream(std::ostream& dst, std::string const& prefix, Fn fn) {
    auto* old = dst.rdbuf();
    FilteringOutputStreamBuf prefix_buf(old, LinePrefixingFilter{prefix});
    dst.rdbuf(&prefix_buf);
    fn();
    dst.flush();
    dst.rdbuf(old);
}

#endif // UTIL_IO_H
