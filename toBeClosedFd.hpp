#pragma once
#include <unistd.h>
#include <utility>

using fileDescriptor = int;

struct FDDeleter {
	constexpr static fileDescriptor invalidValue = -1;

	void operator()(fileDescriptor& fd) const noexcept  {
		if (is_valid(fd)) {
			close(fd);
			fd = -1;
		}
	}
	constexpr bool is_valid(const fileDescriptor& fd) const noexcept {
		return fd >=0;
	}
};

template<typename ContainedType, typename Deleter>
class [[nodiscard]] ToBeClosedGeneric :public Deleter {
protected:
	ContainedType fd;
	void tryClose() noexcept {
		Deleter::operator()(fd);
	}
public:
	using value_type = ContainedType;
	using del = Deleter;
	
	explicit constexpr ToBeClosedGeneric(ContainedType fd) noexcept : fd(fd) {}
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
	constexpr void swap(ToBeClosedGeneric& other) noexcept {
		std::swap(fd, other.fd);
	}
	
	[[nodiscard]] constexpr ContainedType release() noexcept {
		return std::exchange(fd, Deleter::invalidValue);
	}

	constexpr ContainedType get() const noexcept {
		return fd;
	}

	void reset(ContainedType other = Deleter::invalidValue) noexcept {
		tryClose();
		fd = other;
	}

	constexpr explicit operator ContainedType() const noexcept {
		return fd;
	}
	constexpr explicit operator bool() const noexcept {
		return  Deleter::is_valid(fd);
	}
};

using ToBeClosedFd = ToBeClosedGeneric<fileDescriptor, FDDeleter>;