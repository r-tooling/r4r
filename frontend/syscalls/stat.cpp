#include "stat.hpp"
#include <fcntl.h> //AT flags
#include <string_view>
namespace frontend::SyscallHandlers {
	void StatHandler::exit(processState&, middleend::MiddleEndState&, long syscallRetval)
	{
		/*
			These functions return information about a file.
			No permissions are required on the file itself,
			but-in the case of stat() and lstat() - execute (search) permission is required on all of the directories in path that lead to the file.
		*/
		if (syscallRetval < 0) {
			//todo: handle errors and perhaps mention the lack of access rights
		}

		//TODO: add directory access information to the middle end
		if (path == "" && (flags & AT_EMPTY_PATH)) { //we are operating on the - technically the flag is linux specific but I do use it internally so I can only have one handler.
			//todo: operation on the FD - or in the case of AT_FDCWD current dir
		}
		else {
			//TODO: handle the operation on the path, logging components and such
		}

	}

	void StatHandler::entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr)
	{
		simpleSyscallHandler_base::entryLog(process, state, syscallNr);

		strBuf << "(";
		appendResolvedFilename<false>(process, state, at, strBuf); 
		strBuf << "," << path << "," << flags << ")";
	}

	void Stat::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		at = AT_FDCWD;
		path = process.ptrToStr<relFilePath>(process.getSyscallParam<1>());
		flags = 0;
	}

	void FStat::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		at = process.getSyscallParam<1>();
		path = "";
		flags = AT_EMPTY_PATH;
	}

	void LStat::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		at = AT_FDCWD;
		path = process.ptrToStr<relFilePath>(process.getSyscallParam<1>());
		flags = AT_SYMLINK_NOFOLLOW;
	}

	void NewFStatAt::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		at = process.getSyscallParam<1>();
		path = process.ptrToStr<relFilePath>(process.getSyscallParam<2>());
		flags = process.getSyscallParam<3>();
	}


	void StatX::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		at = process.getSyscallParam<1>();
		path = process.ptrToStr<relFilePath>(process.getSyscallParam<2>());
		flags = process.getSyscallParam<3>();
	}


	void AccessHandler::exit(processState&, middleend::MiddleEndState&, long)
	{
		//TODO: we care for the access rights to the given files as they shall be mirrored 1:1
	}

	void AccessHandler::entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr)
	{
		simpleSyscallHandler_base::entryLog(process, state, syscallNr);

		strBuf << "(";
		appendResolvedFilename(process, state, at, strBuf);
		strBuf << "," << path << "," << flags << ")";
	}

	void Access::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		at = AT_FDCWD;
		path = process.ptrToStr<relFilePath>(process.getSyscallParam<1>());
		flags = 0;
		//todo: mode ignored?
	}

	void FAccessAt::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		at = process.getSyscallParam<1>();
		path = process.ptrToStr<relFilePath>(process.getSyscallParam<2>());
		flags = 0;
	}

	void FAccessAt2::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		at = process.getSyscallParam<1>();
		path = process.ptrToStr<relFilePath>(process.getSyscallParam<2>());
		flags = process.getSyscallParam<4>();
	}

	void ReadLinkHandler::exit(processState& process, middleend::MiddleEndState&, long syscallRetval)
	{
		//TODO: we care for symoblic links as we need to re-create these mappings
		if (syscallRetval > 0) {
			if (syscallRetval == static_cast<long>(maxBufferSize)) {
				//todo: add handling that this linkdata may be incomplete and should be treated as such.
			}
			returnedData = userPtrToOwnPtr(process.pid, userPtr, syscallRetval); //todo: check the validity based on the string being 0 terminated perhaps?
		}
		//AT_EMPTY_PATH implicit
	}

	void ReadLinkHandler::entryLog(const processState& process, const middleend::MiddleEndState& state, long syscallNr)
	{
		simpleSyscallHandler_base::entryLog(process, state, syscallNr);

		strBuf << "(";
		appendResolvedFilename(process, state, at, strBuf);
		strBuf << "," << path << ")";
	}

	void ReadLinkHandler::exitLog(const processState& process, const middleend::MiddleEndState& state, long syscallRetval)
	{
		if (syscallRetval > 0) {
			auto str = strBuf.str();
			printf("%d: %s = ", process.pid, str.empty() ? "Unknown syscall" : str.c_str());
			fflush(stdout);
			auto written = write(fileno(stdout), returnedData.get(), syscallRetval);
			written = write(fileno(stdout), "\n", 1);
			(void)written; //thsi is marked nodiscard as it should be chacked. But at the end of the day, this not writing anything is fine.

			strBuf.clear();
		}
		else {
			simpleSyscallHandler_base::exitLog(process, state, syscallRetval);
		}
	}

	void ReadLink::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		at = AT_FDCWD;
		path = process.ptrToStr<relFilePath>(process.getSyscallParam<1>());
		userPtr = process.getSyscallParam<2>();
		maxBufferSize = process.getSyscallParam<3>();
	}

	void ReadLinkAt::entry(processState& process, const middleend::MiddleEndState&, long)
	{
		at = process.getSyscallParam<1>();
		path = process.ptrToStr<relFilePath>(process.getSyscallParam<2>());
		userPtr = process.getSyscallParam<3>();
		maxBufferSize = process.getSyscallParam<4>();
	}
}