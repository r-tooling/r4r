// FIXME: delete!
#pragma once
#include <unistd.h>
#include <utility>
/*
 * A semi-portable way of getting the type of a file handle.
 * It will not work on a Windows absed system due to a different underlying
 * system. But then everything has to be rewritten.
 */
using fileDescriptor = decltype(fileno(stdin));

/*An implementation of a deleter class, all defined here is required.*/
struct FDDeleter {
    constexpr static fileDescriptor invalidValue = -1;

    void operator()(fileDescriptor& fd) const noexcept {
        if (is_valid(fd)) {
            close(fd);
            fd = -1;
        }
    }
    constexpr bool is_valid(const fileDescriptor& fd) const noexcept {
        return fd >= 0;
    }
};
/*
 * A generic implementation of a unique_ptr which may work with values other
 * than pointers. The API is basically the same as with unique_ptr
 */
template <typename ContainedType, typename Deleter>
class [[nodiscard]] ToBeClosedGeneric : public Deleter {
    static_assert(std::is_nothrow_swappable_v<ContainedType>,
                  "The Generic descriptor wrapper is designed to move around "
                  "values, cannot affor an exception there, if you want a copy "
                  "based one, do it yourself");

  protected:
    ContainedType fd;
    void tryClose() noexcept { Deleter::operator()(fd); }

  public:
    using value_type = ContainedType;
    using del = Deleter;

    explicit constexpr ToBeClosedGeneric(ContainedType fd) noexcept : fd(fd) {}
    constexpr ToBeClosedGeneric(const ToBeClosedGeneric& fd) = delete;
    constexpr ToBeClosedGeneric(ToBeClosedGeneric&& other) noexcept
        : fd(other.release()) {}

    ToBeClosedGeneric& operator=(const ToBeClosedGeneric&) = delete;
    ToBeClosedGeneric& operator=(ToBeClosedGeneric&& other) noexcept {
        tryClose();
        fd = other.release();
        return *this;
    }

    ~ToBeClosedGeneric() noexcept { tryClose(); }
    constexpr void swap(ToBeClosedGeneric& other) noexcept {
        std::swap(fd, other.fd);
    }

    [[nodiscard]] constexpr ContainedType release() noexcept {
        return std::exchange(fd, Deleter::invalidValue);
    }

    constexpr ContainedType get() const noexcept { return fd; }

    void reset(ContainedType other = Deleter::invalidValue) noexcept {
        tryClose();
        fd = other;
    }

    constexpr explicit operator ContainedType() const noexcept { return fd; }
    constexpr explicit operator bool() const noexcept {
        return Deleter::is_valid(fd);
    }
};
/*
 * A unique-ptr like wrapper around the unix native file handle
 */
using ToBeClosedFd = ToBeClosedGeneric<fileDescriptor, FDDeleter>;
