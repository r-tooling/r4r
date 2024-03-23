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
		std::size_t offset;
		const T val;
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

inline std::string userPtrToString(pid_t processPid, long processPtr) {
	std::vector<char> v;
	bool zeroReched = false;
	std::size_t offset = 0;
	while (!zeroReched) {
		errno = 0;
		auto res = ptrace(PTRACE_PEEKDATA, processPid, processPtr + (offset * sizeof(processPtr)), nullptr);
		if (res == -1 && errno != 0) {
			
			return {};//try to continue as if nothing happened. will probably break everything.
		}
		for (auto i : BytesIterator(res)) {
			if (i == 0) {
				zeroReched = true;
				break;
			}
			v.push_back(i);//this "needs" to be here as otherwise we create a string which contains a 0 byte as the last byte while sctr compare will be fine, string comapre will fail.
		}
		++offset;
	}
	//reallocations are unfortunatelly bound to happen here a lot. I need to sloooowly parse the entire string utilll I get to the 0 byte.
	//consider process_vm_readv instead for reading larger blocks at once. Or any other adress space-sharing mechanism.
	return std::string(v.begin(), v.end());
}


inline std::unique_ptr<unsigned char[]> userPtrToOwnPtr(pid_t processPid, long ptr, size_t count) {
	iovec remote[1];
	remote[0].iov_base = reinterpret_cast<void*>(ptr);
	remote[0].iov_len = count;
	iovec local[1];
	auto localPtr = std::make_unique<unsigned char[]>(count);
	local->iov_base = reinterpret_cast<void*>(localPtr.get());
	local->iov_len = count;

	assert(process_vm_readv(processPid, local, 1, remote, 1, 0) == count);// "error reading X bytes from the tracee"
	return std::move(localPtr);
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

	assert(process_vm_readv(processPid, local, 1, remote, 1, 0) == count);// "error reading X bytes from the tracee"
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

	assert(process_vm_readv(processPid, local, 1, remote, 1, 0) == count);// "error reading X bytes from the tracee"
	return { reinterpret_cast<pointsToStruct*>(localPtr),std::free };
}