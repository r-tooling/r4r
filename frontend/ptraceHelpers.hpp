#pragma once

#include <cstdio>
#include <vector>
#include <string>
#include <memory>
#include <cstdlib>
#include <type_traits>

#include <sys/ptrace.h>
#include <sys/uio.h> //process_vm_readv


#ifdef __INTELLISENSE__ //magic because the msvc intellisesnse breaks on includes of the gcc stdlib
namespace std {
	template<class _To, class _From>
	constexpr _To bit_cast(const _From& _Val) noexcept {
		return reinterpret_cast<_To>(_Val);
	}
}
#endif


#include <bit>


template<class T>
struct BytesIterator {
	const T val;
	constexpr BytesIterator(const T v) : val(v) {}
private:
	struct sentinel {

	};

	static constexpr unsigned char getByte(const T v, const std::size_t offset) {
		std::make_unsigned_t<T> real = std::bit_cast<std::make_unsigned_t<T>>(v);
		if constexpr (std::endian::native == std::endian::little) {
			real >>= offset * 8;
			return static_cast<unsigned char>(real & std::make_unsigned_t<T>{0xFF});
		}
		else {
			auto mask = std::make_unsigned_t<T>{ 0xFF } << offset * 8;
			return static_cast<unsigned char>(real & mask) >> offset * 8;
		}
	}

public:


	struct const_iterator {
		const T val;
		std::size_t offset;
		constexpr const_iterator(const T& p) :val(p), offset(0) {}
		constexpr const_iterator(sentinel) : val{}, offset(sizeof(T)) {}

		constexpr const_iterator& operator+(const std::size_t offset) {
			this->offset += offset;
			return *this;
		}
		constexpr const_iterator& operator++() {
			++offset;
			return *this;
		}
		constexpr unsigned char operator*() {
			return getByte(val, offset);
		}
		constexpr auto operator<=>(const const_iterator& other) {
			return offset <=> other.offset;
		}
		constexpr auto operator!=(const const_iterator& other) {
			return offset != other.offset;
		}
	};
	constexpr const_iterator begin() const {
		return { val };
	}
	constexpr const_iterator end() const {
		return { sentinel{} };
	}
};

inline void logPtraceError(int err) {
	switch (err) {
	case EBUSY: printf("(i386 only) There was an error with allocating or freeing a debug register."); break;
	case EFAULT: printf("There was an attempt to read from or write to an invalid"
		"area in the tracer's or the tracee's memory, probably"
		"because the area wasn't mapped or accessible."
		"Unfortunately, under Linux, different variations of this"
		"fault will return EIO or EFAULT more or less arbitrarily."); break;
	case EINVAL: printf("An attempt was made to set an invalid option."); break;
	case EIO: printf("request is invalid, or an attempt was made to read from or"
		"write to an invalid area in the tracer's or the tracee's"
		"memory, or there was a word - alignment violation, or an"
		"invalid signal was specified during a restart request."); break;
	case EPERM: printf("he specified process cannot be traced. This could be"
		"because the tracer has insufficient privileges(the"
		"required capability is CAP_SYS_PTRACE); unprivileged"
		"processes cannot trace processes that they cannot send"
		"signals to or those running set - user - ID / set - group - ID"
		"programs, for obvious reasons.Alternatively, the process"
		"may already be being traced, or (before Linux 2.6.26) be"
		"init(1) (PID 1)."); break;
	case ESRCH: printf("request is invalid, or an attempt was made to read from or"
		"write to an invalid area in the tracer's or the tracee's"
		"memory, or there was a word - alignment violation, or an"
		"invalid signal was specified during a restart request."); break;
	default: printf("unknown err"); break;
	}
}
namespace frontend {

	//using PTRACE_PEEKDATA is very slow. And it results in  thousands of calls. Instead I querry for a bunch of values at once and hope that the string is not at a nasty boundary which would require more handling which is currently not present.
	template<class result = std::string>
	inline result userPtrToString(pid_t processPid, const long processPtr) {
		constexpr static size_t readSize = 32;
		constexpr static size_t bufferSize = 256;
		constexpr static size_t bufferCount = bufferSize /readSize;

		std::vector<char> v;

		std::size_t offset = 0;

		iovec local[bufferCount];
		iovec remote[bufferCount];

		auto resizeBuffer = [&]() {
			v.resize(v.size() + bufferSize);
			for (size_t i = 0; i < bufferCount; i++) {
				char* buffer = v.data() + offset;
				local[i].iov_base = &buffer[i * readSize];
				local[i].iov_len = readSize;
				remote[i].iov_base = reinterpret_cast<void*>(processPtr + ((offset + (i*readSize))));
				remote[i].iov_len = readSize;
			}
		};


		bool zeroReched = false;
		bool readAll = true;
		while (!zeroReched && readAll) {
			resizeBuffer();
			auto read_count = process_vm_readv(processPid, local, bufferCount, remote, bufferCount, 0);
			if (read_count != bufferSize) {//TODO: error logs and checks?
				readAll = false;
			}
			else if (read_count < 0) {
				break;//todo: log error
			}
			auto end = v.end() - (bufferSize - read_count);
			for (auto it = v.begin() + offset; it != end; ++it) {
				if (*it == 0) {
					zeroReched = true;
					v.erase(it, v.end());//the zero is automagically appended in the string
					break;
				}
			}
			offset += read_count;
		}
		//reallocations are unfortunatelly bound to happen here a lot. I need to sloooowly parse the entire string utilll I get to the 0 byte.
		//consider process_vm_readv instead for reading larger blocks at once. Or any other adress space-sharing mechanism.
		return { v.begin(), v.end() };
	}


	inline std::unique_ptr<unsigned char[]> userPtrToOwnPtr(pid_t processPid, long ptr, size_t count) {
		iovec remote[1] = {
			iovec{
			.iov_base = reinterpret_cast<void*>(ptr),
			.iov_len = count
			}
		};
		iovec local[1];
		auto localPtr = std::make_unique<unsigned char[]>(count);
		local->iov_base = reinterpret_cast<void*>(localPtr.get());
		local->iov_len = count;
		auto read_count = process_vm_readv(processPid, local, 1, remote, 1, 0);
		(void)read_count;
		assert(read_count == static_cast<ssize_t>(count));// "error reading X bytes from the tracee"
		return localPtr;
	}
	template<class pointsToStruct>
	inline std::unique_ptr<pointsToStruct, decltype(std::free)*> userPtrToOwnPtr(pid_t processPid, long ptr) {
		constexpr size_t count = sizeof(pointsToStruct);
		iovec remote[1];
		remote[0].iov_base = reinterpret_cast<void*>(ptr);
		remote[0].iov_len = count;
		iovec local[1];
		auto localPtr = malloc(count);
		local->iov_base = localPtr;
		local->iov_len = count;
		auto read_count = process_vm_readv(processPid, local, 1, remote, 1, 0);
		(void)read_count;
		assert(read_count == count);// "error reading X bytes from the tracee"
		return { reinterpret_cast<pointsToStruct*>(localPtr),std::free };
	}
	//TODO: figure out how to make it possible to specify the array size in the return type.
	template<class pointsToStruct, int Count>
	inline std::unique_ptr<pointsToStruct[], decltype(std::free)*> userPtrToOwnPtr(pid_t processPid, long ptr) {
		constexpr size_t count = sizeof(pointsToStruct) * Count;
		iovec remote[1];
		remote[0].iov_base = reinterpret_cast<void*>(ptr);
		remote[0].iov_len = count;
		iovec local[1];
		auto localPtr = malloc(count);
		local->iov_base = localPtr;
		local->iov_len = count;
		auto read_count = process_vm_readv(processPid, local, 1, remote, 1, 0);
		(void)read_count;
		assert(read_count == count);// "error reading X bytes from the tracee"
		return { reinterpret_cast<pointsToStruct*>(localPtr),std::free };
	}
}