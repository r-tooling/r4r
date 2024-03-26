#include "stat.hpp"
#include <fcntl.h> //AT flags
#include <string_view>

void SyscallHandlers::StatHandler::exit(processState& process, MiddleEndState& state, long syscallRetval)
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

void SyscallHandlers::StatHandler::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr);
	
	strBuf << "(";
	appendResolvedFilename(process,state,at,strBuf);
	strBuf << "," << path << "," << flags << ")";
}

void SyscallHandlers::Stat::entry(processState & process, const MiddleEndState& state, long syscallNr)
{
	at = AT_FDCWD;
	path = userPtrToString(process.pid,getSyscallParam<1>(process.pid));
	flags = 0;
}

void SyscallHandlers::FStat::entry(processState & process, const MiddleEndState& state, long syscallNr)
{
	at = getSyscallParam<1>(process.pid);
	path = "";
	flags = AT_EMPTY_PATH;
}

void SyscallHandlers::LStat::entry(processState & process, const MiddleEndState& state, long syscallNr)
{
	at = AT_FDCWD;
	path = userPtrToString(process.pid, getSyscallParam<1>(process.pid));
	flags = AT_SYMLINK_NOFOLLOW;
}

void SyscallHandlers::NewFStatAt::entry(processState & process, const MiddleEndState& state, long syscallNr)
{
	at = getSyscallParam<1>(process.pid);
	path = userPtrToString(process.pid, getSyscallParam<2>(process.pid));
	flags = getSyscallParam<3>(process.pid);
}


void SyscallHandlers::StatX::entry(processState & process, const MiddleEndState& state, long syscallNr)
{
	at = getSyscallParam<1>(process.pid);
	path = userPtrToString(process.pid, getSyscallParam<2>(process.pid));
	flags = getSyscallParam<3>(process.pid);
}


void SyscallHandlers::AccessHandler::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	//TODO: we care for the access rights to the given files as they shall be mirrored 1:1
}

void SyscallHandlers::AccessHandler::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr);

	strBuf << "(";
	appendResolvedFilename(process, state, at, strBuf);
	strBuf << "," << path << "," << flags << ")";
}

void SyscallHandlers::Access::entry(processState& process, const MiddleEndState& state, long syscallNr)
{
	at = AT_FDCWD;
	path = userPtrToString(process.pid, getSyscallParam<1>(process.pid));
	flags = 0;
	//todo: mode ignored?
}

void SyscallHandlers::FAccessAt::entry(processState& process, const MiddleEndState& state, long syscallNr)
{
	at = getSyscallParam<1>(process.pid);
	path = userPtrToString(process.pid, getSyscallParam<2>(process.pid));
	flags = 0;
}

void SyscallHandlers::FAccessAt2::entry(processState& process, const MiddleEndState& state, long syscallNr)
{
	at = getSyscallParam<1>(process.pid);
	path = userPtrToString(process.pid, getSyscallParam<2>(process.pid));
	flags = getSyscallParam<4>(process.pid);
}

void SyscallHandlers::ReadLinkHandler::exit(processState& process, MiddleEndState& state, long syscallRetval)
{
	//TODO: we care for symoblic links as we need to re-create these mappings
	if (syscallRetval > 0) {
		if (syscallRetval == maxBufferSize) {
			//todo: add handling that this linkdata may be incomplete and should be treated as such.
		}
		returnedData = userPtrToOwnPtr(process.pid, userPtr,syscallRetval); //todo: check the validity based on the string being 0 terminated perhaps?
	}
	//AT_EMPTY_PATH implicit
}

void SyscallHandlers::ReadLinkHandler::entryLog(const processState& process, const MiddleEndState& state, long syscallNr)
{
	simpleSyscallHandler_base::entryLog(process, state, syscallNr);

	strBuf << "(";
	appendResolvedFilename(process, state, at, strBuf);
	strBuf << "," << path << ")";
}

void SyscallHandlers::ReadLinkHandler::exitLog(const processState& process, const MiddleEndState& state, long syscallRetval)
{
	if (syscallRetval > 0) {
		auto str = strBuf.str();
		printf("%d: %s = ", process.pid, str.empty() ? "Unknown syscall" : str.c_str());
		fflush(stdout);
		write(fileno(stdout), returnedData.get(), syscallRetval);//TODO: use c++ stringview instead perhaps?
		write(fileno(stdout), "\n", 1);
		strBuf.clear();
	}
	else {
		simpleSyscallHandler_base::exitLog(process, state, syscallRetval);
	}
}

void SyscallHandlers::ReadLink::entry(processState& process, const MiddleEndState& state, long syscallNr)
{
	at = AT_FDCWD;
	path = userPtrToString(process.pid, getSyscallParam<1>(process.pid));
	userPtr = getSyscallParam<2>(process.pid);
	maxBufferSize = getSyscallParam<3>(process.pid);
}

void SyscallHandlers::ReadLinkAt::entry(processState& process, const MiddleEndState& state, long syscallNr)
{
	at = getSyscallParam<1>(process.pid);
	path = userPtrToString(process.pid, getSyscallParam<2>(process.pid));
	userPtr = getSyscallParam<3>(process.pid);
	maxBufferSize = getSyscallParam<4>(process.pid);
}
