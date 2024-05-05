#include "open.hpp"
#include <fcntl.h>
#include <linux/openat2.h>
#include <sys/stat.h>
#include <cassert>
namespace frontend::SyscallHandlers {

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
	void OpenBase::exit(processState& process, middleend::MiddleEndState& state, long syscallRetval)
	{
		auto FD = static_cast<fileDescriptor>(syscallRetval);
		if (FD < 0) {
			//TODO: log open failed and file is non existent. Check retval for error.
		}
		else {
			auto resolvedPath = state.resolveToAbsoltute(process.pid, fileRelPath, at);
			assert(resolvedPath.is_absolute());
			state.openHandling(process.pid, resolvedPath, std::move(fileRelPath), FD, flags, existed);//the order is undefined otherwise

		}
	}

	void OpenBase::entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr)
	{
		simpleSyscallHandler_base::entryLog(process, state, syscallNr);
		strBuf << "(" << fileRelPath << ")";
	}

	void OpenBase::checkIfExists(const processState& process, const middleend::MiddleEndState& middleEnd)
	{
		if (!(flags & O_CREAT)) { //had already existed, if it did indeed exist, then we just error out
			existed = true;
			return;
		}
		existed = middleEnd.checkFileExists(process.pid,at,fileRelPath,flags);
		return;
	}


	void Open::entry(processState& process, const middleend::MiddleEndState& state, long)
	{
		fileRelPath = userPtrToString(process.pid, getSyscallParam<1>(process.pid));
		flags = getSyscallParam<2>(process.pid);
		at = AT_FDCWD;
		checkIfExists(process,state);
	}

	void OpenAT::entry(processState& process, const middleend::MiddleEndState& state, long)
	{
		fileRelPath = std::filesystem::path{ userPtrToString(process.pid, getSyscallParam<2>(process.pid)) };
		at = getSyscallParam<1>(process.pid);
		flags = getSyscallParam<3>(process.pid);
		checkIfExists(process, state);
	}


	void OpenAT2::entry(processState& process, const middleend::MiddleEndState& state, long)
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
		checkIfExists(process, state);
	}

	void Creat::entry(processState& process, const middleend::MiddleEndState& state, long)
	{
		//I am the open syscall with a couple of flags. https://github.com/torvalds/linux/blob/484193fecd2b6349a6fd1554d306aec646ae1a6a/fs/open.c#L1493
		fileRelPath = std::filesystem::path{ userPtrToString(process.pid, getSyscallParam<2>(process.pid)) };
		at = AT_FDCWD;
		flags = O_CREAT | O_WRONLY | O_TRUNC;
		checkIfExists(process, state);
	}
}