#pragma once
#include <unistd.h>
#include <utility>

using fileDescriptor = int;

template<typename ContainedType>
class ToBeClosedGeneric {
protected:
	ContainedType fd;

	virtual void tryClose() noexcept {
		if (fd >= 0) {
			close(fd);
		}
	}
public:
	
	constexpr ToBeClosedGeneric(ContainedType fd) noexcept : fd(fd) {}
	constexpr ToBeClosedGeneric(const ToBeClosedGeneric& fd) = delete;
	constexpr ToBeClosedGeneric(ToBeClosedGeneric&& other) noexcept : fd(other.release()) {}

	ToBeClosedGeneric&  operator =(const ToBeClosedGeneric&) = delete;
	ToBeClosedGeneric& operator =(ToBeClosedGeneric&& other) noexcept {
		tryClose();
		fd = other.release();
		return *this;
	}

	~ToBeClosedGeneric() noexcept {
		tryClose();
	}
	
	constexpr ContainedType release() noexcept {
		return std::exchange(fd, -1);
	}

	constexpr ContainedType get() const noexcept {
		return fd;
	}

	void reset(ContainedType other = -1) noexcept {
		tryClose();
		fd = other;
	}

	constexpr explicit operator ContainedType() const noexcept {
		return fd;
	}
	constexpr explicit operator bool() {
		return fd >= 0;
	}
};

using ToBeClosedFd = ToBeClosedGeneric<fileDescriptor>;