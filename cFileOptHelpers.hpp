#pragma once
#include "./toBeClosedFd.hpp"
#include <string_view>
#include <cstdio>
#include <cstdlib>

struct FilePtrDeleter {
	constexpr static FILE* invalidValue = nullptr;

	void operator()(FILE*& fp) const noexcept {
		if (fp) {
			fclose(fp);
			fp = nullptr;
		}
	}
	constexpr bool is_valid(FILE* const& fp) const noexcept{
		return fp != nullptr;
	}
};

using ToBeClosedFile = ToBeClosedGeneric<FILE*, FilePtrDeleter>;


struct ToBeClosedFileFD : protected ToBeClosedFile {
	fileDescriptor underlyingFD;
	/*
	the file descriptor is not dup'ed, 
	and will be closed when the stream created by fdopen() is closed. 
	The result of applying fdopen() to a shared memory object is undefined. */
	explicit ToBeClosedFileFD(ToBeClosedFd fd, const char* mode) 
		noexcept(noexcept(ToBeClosedFile(fdopen(fd.release(), mode))))
		: ToBeClosedFile(fd ? fdopen(fd.get(), mode) : nullptr),underlyingFD(fd.release()) {} //technically I shall be releasing into the parent call but I need to store the get value here...

	explicit operator fileDescriptor() {
		return underlyingFD;
	}
	constexpr ToBeClosedFile::value_type release() noexcept {
		underlyingFD = -1;
		return ToBeClosedFile::release();
	}

	void reset(ToBeClosedFile::value_type other = ToBeClosedFile::del::invalidValue) noexcept {
		tryClose();
		fd = other;
		if (fd) {
			underlyingFD = fileno(fd);
		}
		else {
			underlyingFD = -1;
		}
	}
	constexpr ToBeClosedFile::value_type get() const noexcept {
		return fd;
	}

	constexpr explicit operator ToBeClosedFile::value_type() const noexcept {
		return ToBeClosedFile::operator ToBeClosedFile::value_type();
	}
	constexpr explicit operator bool() {
		return ToBeClosedFile::operator bool();
	}
};

template<class T>
struct freeDeleter {
	static_assert(std::is_trivially_destructible_v<T>);
	constexpr static T** invalidValue = nullptr;

	void operator()(T*& ptr) const noexcept {
		if (ptr) {
			free(ptr);
			ptr = nullptr;
		}
	}
	constexpr bool is_valid(T* const& fp) const noexcept {
		return fp != nullptr;
	}

};
template<class Value>
using FreeUniquePtr = ToBeClosedGeneric<Value*, freeDeleter<Value>>;

template<class T>
struct OpaqueUniquePtr : public ToBeClosedGeneric<T*, freeDeleter<T>> {
	using ToBeClosedGeneric<T*, freeDeleter<T>>::ToBeClosedGeneric;

	T*& getRef() {
		return this->fd;
	}
};

struct LineIterator {
	FILE* file;

	LineIterator(FILE* file) :file(file) {};

	struct EndSentinel {

	};

	struct IteratorImpl {
		OpaqueUniquePtr<char> buffer{ nullptr };
		size_t bufferSize = 0;

		FILE* file;
		ssize_t nrRead = 0;

		void doRead() {
			if (nrRead >= 0) { //no error
				nrRead = getdelim(&buffer.getRef(), &bufferSize, '\n', file);
			}
		}

		IteratorImpl(FILE* file) :file(file) {
			if(file)
				doRead();
		}
		IteratorImpl& operator++() {
			doRead();
			return *this;
		}

		bool operator ==(const EndSentinel&) const {
			return nrRead <0;
		}
		bool operator !=(const EndSentinel&) const {
			return nrRead >= 0;
		}
		std::string_view operator*() {
			if (nrRead < 0) {
				return {};
			}
			return { buffer.get(),static_cast<size_t>(nrRead)};
		}
	
	};

	auto end() {
		return EndSentinel{};
	}
	auto begin() {
		return IteratorImpl{file};
	}
};