#include "open.hpp"
#include <fcntl.h>
#include <linux/openat2.h>
#include <sys/stat.h>
#include <cassert>
/*
			The dirfd argument is used in conjunction with the pathname
		   argument as follows:

		   •  If the pathname given in pathname is absolute, then dirfd is
			  ignored.

		   •  If the pathname given in pathname is relative and dirfd is the
			  special value AT_FDCWD, then pathname is interpreted relative
			  to the current working directory of the calling process (like
			  open()).

		   •  If the pathname given in pathname is relative, then it is
			  interpreted relative to the directory referred to by the file
			  descriptor dirfd (rather than relative to the current working
			  directory of the calling process, as is done by open() for a
			  relative pathname).  In this case, dirfd must be a directory
			  that was opened for reading (O_RDONLY) or using the O_PATH
			  flag.

		   If the pathname given in pathname is relative, and dirfd is not a
		   valid file descriptor, an error (EBADF) results.  (Specifying an
		   invalid file descriptor number in dirfd can be used as a means to
		   ensure that pathname is absolute.)
			*/
void OpenBase::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	auto FD = static_cast<fileDescriptor>(syscallRetval);
	if (FD < 0) {
		//TODO: log open failed and file is non existent. Check retval for error.
	}
	else {
		assert(at == AT_FDCWD || fileRelPath.is_absolute());
		std::unique_ptr<char, decltype(std::free)*> resolvedPath{ realpath(fileRelPath.c_str(), nullptr), std::free };//avoid having to re-implement this in the middle end and possibly add more bugs in the implementation. TODD: handle chroot.

		if (!resolvedPath) {
			printf("Unable to resolve path for file %s\n", fileRelPath.c_str());
			std::string tmp{ fileRelPath };
			state.openHandling(process.pid, std::move(tmp), std::move(fileRelPath), FD, flags,existed);//the order is undefined otherwise
		}
		else {
			state.openHandling(process.pid, { resolvedPath.get() }, std::move(fileRelPath), FD, flags, existed);//the order is undefined otherwise
		}
	}
}

void OpenBase::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr);
	strBuf << "(" << fileRelPath << ")";
}

void OpenBase::checkIfExists(const processState& process)
{
	if (flags & O_CREAT) { //had already existed, if it did indeed exist, then we just error out
		existed = false;
		return;
	}
	struct stat data;
	int state;
	int err = 0;
	//TODO: what if chdir or chroot?
	//TODO: how about other flags
	//TODO: check if we already know about file in middleEnd
	if (at != AT_FDCWD) {
		fileDescriptor toBeClosed = process.stealFD(at);
		state = fstatat(toBeClosed, fileRelPath.c_str(), &data, (flags & O_NOFOLLOW) ? AT_SYMLINK_NOFOLLOW : 0);
		err = errno;
		close(toBeClosed);
	}
	else {
		state =fstatat(AT_FDCWD, fileRelPath.c_str(), &data, (flags & O_NOFOLLOW) ? AT_SYMLINK_NOFOLLOW : 0);
		err = errno;
	}
	assert(state == 0 || (state = -1 && err == ENOENT)); //todo: handle other errors
	existed = state == 0;
}


void Open::entry(const processState& process, const MiddleEndState& state, long syscallNr)
{
	fileRelPath = userPtrToString(process.pid, getSyscallParam<1>(process.pid));
	flags = getSyscallParam<2>(process.pid);
	at = AT_FDCWD;
	checkIfExists(process);
}

void OpenAT::entry(const processState& process, const MiddleEndState& state, long syscallNr)
{
	fileRelPath = std::filesystem::path{ userPtrToString(process.pid, getSyscallParam<2>(process.pid)) };
	at = getSyscallParam<1>(process.pid);
	flags = getSyscallParam<3>(process.pid);
	checkIfExists(process);
}


void OpenAT2::entry(const processState& process, const MiddleEndState& state, long syscallNr)
{
	auto structSize = getSyscallParam<4>(process.pid);
	assert(structSize == sizeof(open_how));
	auto ptr = getSyscallParam<3>(process.pid);
	auto mine = userPtrToOwnPtr(process.pid, ptr, structSize);
	auto how = reinterpret_cast<open_how*>(mine.get());

	//todo: support all those magical flags of resolve

	fileRelPath = std::filesystem::path{ userPtrToString(process.pid, getSyscallParam<2>(process.pid)) };
	at = getSyscallParam<1>(process.pid);
	flags = how->flags;
	checkIfExists(process);
}

void Creat::entry(const processState& process, const MiddleEndState& state, long syscallNr)
{
	//I am the open syscall with a couple of flags. https://github.com/torvalds/linux/blob/484193fecd2b6349a6fd1554d306aec646ae1a6a/fs/open.c#L1493
	fileRelPath = std::filesystem::path{ userPtrToString(process.pid, getSyscallParam<2>(process.pid)) };
	at = AT_FDCWD;
	flags = O_CREAT | O_WRONLY | O_TRUNC;
	checkIfExists(process);
}
