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
			state.openHandling(process.pid, resolvedPath, std::move(fileRelPath), FD, flags, statResults);//the order is undefined otherwise

		}
	}

	void OpenBase::entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr)
	{
		simpleSyscallHandler_base::entryLog(process, state, syscallNr);
		strBuf << "(" << fileRelPath << ")";
	}

	void OpenBase::checkIfExists(const processState& process, const middleend::MiddleEndState& middleEnd)
	{
		resolvedPath = middleEnd.resolveToAbsolute(process.pid, fileRelPath, at,false, (flags & AT_SYMLINK_NOFOLLOW) ? middleend::MiddleEndState::nofollow_simlink : 0);
		statResults = middleEnd.checkFileInfo(resolvedPath);
	}


	void Open::entry(processState& process, const middleend::MiddleEndState& state, long)
	{
		fileRelPath = process.ptrToStr<relFilePath>(process.getSyscallParam<1>());
		flags = process.getSyscallParam<2>();
		at = AT_FDCWD;
		checkIfExists(process,state);
	}

	void OpenAT::entry(processState& process, const middleend::MiddleEndState& state, long)
	{
		fileRelPath = process.ptrToStr<relFilePath>(process.getSyscallParam<2>());
		at = process.getSyscallParam<1>();
		flags = process.getSyscallParam<3>();
		checkIfExists(process, state);
	}


	void OpenAT2::entry(processState& process, const middleend::MiddleEndState& state, long)
	{
		auto structSize = process.getSyscallParam<4>();
		assert(structSize == sizeof(open_how));
		auto ptr = process.getSyscallParam<3>();
		auto mine = userPtrToOwnPtr(process.pid, ptr, structSize);
		auto how = reinterpret_cast<open_how*>(mine.get());

		//todo: support all those magical flags of resolve

		fileRelPath = process.ptrToStr<relFilePath>(process.getSyscallParam<2>());
		at = process.getSyscallParam<1>();
		flags = how->flags;
		checkIfExists(process, state);
	}

	void Creat::entry(processState& process, const middleend::MiddleEndState& state, long)
	{
		//I am the open syscall with a couple of flags. https://github.com/torvalds/linux/blob/484193fecd2b6349a6fd1554d306aec646ae1a6a/fs/open.c#L1493
		fileRelPath = process.ptrToStr<relFilePath>(process.getSyscallParam<2>());
		at = AT_FDCWD;
		flags = O_CREAT | O_WRONLY | O_TRUNC;
		checkIfExists(process, state);
	}
}