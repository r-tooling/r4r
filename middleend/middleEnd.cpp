#include "middleEnd.hpp"

#include <cassert>
#include <optional>
#include <fcntl.h>

namespace {
	std::optional<std::string> tryReadShellbang(fileDescriptor fd) {//TODO: can the filename contain spaces?
		char shellbang[2];
		auto bytes = read(fd, shellbang, 2);
		if (bytes == 2 && shellbang[0] == '#' && shellbang[1] == '!') {
			std::string result;
			//we assume the name will be very short. No strbuf
			//essentially matching a #! ?([^ \t\n]*)
			constexpr int byteCount = 1024;
			char bytes[byteCount];
			bool terminated = false;
			bool first = true;
			while (!terminated) {
				auto nrRead = read(fd, bytes, byteCount);
				if (nrRead <= 0) //TODO: error handling
					break;
				terminated = terminated || nrRead != byteCount;
				std::string_view parsedStr(bytes, bytes + nrRead);

				if (first) {
					if (parsedStr[0] == ' ')
						parsedStr = parsedStr.substr(1);
					first = false;
				}
				
				if (auto found = parsedStr.find_first_of(" \n\t"); found != std::string_view::npos) {
					parsedStr = parsedStr.substr(0, found);
					terminated = true;
				}
				result += parsedStr;
			}
			return result;
		}
		else {
			return std::nullopt;
		}
	}
}


void MiddleEndState::trackNewProcess(pid_t process)
{
	auto val = processToInfo.find(process);
	if (val != processToInfo.end()) { //I ahve already been created by teh other call. TODO: ensure this does not happen.
		return;
	}

	FD_Table* rawPtr;
	{
		auto tbl = std::make_unique<MiddleEndState::FD_Table>();
		rawPtr = tbl.get();
		FD_Tables.push_back(std::move(tbl));
	}
	std::unique_ptr<char, decltype(std::free)*>workdir{get_current_dir_name(), std::free};

	processToInfo.emplace(process, MiddleEndState::running_thread_state{ process, std::filesystem::path{workdir.get()} ,rawPtr});
	if (processToInfo.size() == 1) {//TODO: a better solution for the initial setup?
		auto in = new file_info{ 
			.realpath = "stdin",
			.wasEverCreated = false,
			.wasEverDeleted = false,
			.isCurrentlyOnTheDisk = false,
			.wasInitiallyOnTheDisk = false,
			.type = file_info::pipe};
		nonFileDescriptors.emplace_back(in);
		rawPtr->table.emplace(0, in);
		auto& out = nonFileDescriptors.emplace_back(new file_info{
			.realpath = "stdout",
			.wasEverCreated = false,
			.wasEverDeleted = false,
			.isCurrentlyOnTheDisk = false,
			.wasInitiallyOnTheDisk = false,
			.type = file_info::pipe
			});
		rawPtr->table.emplace(1, out.get());
		auto& err = nonFileDescriptors.emplace_back(new file_info{ 
			.realpath = "stderr",
			.wasEverCreated = false,
			.wasEverDeleted = false,
			.isCurrentlyOnTheDisk = false,
			.wasInitiallyOnTheDisk = false,
			.type = file_info::pipe
			});
		rawPtr->table.emplace(2, err.get());
	}
}

void MiddleEndState::trackNewProcess(pid_t process, pid_t parent, bool copy, std::optional<pid_t> assumedChildPid)
{
	if (assumedChildPid.has_value() && assumedChildPid != process) {
		//we assigned wrong!
		assert(false);//todo: error handling
	}
	//TODO: vfork issues.
	if(processToInfo.find(process) == processToInfo.end())
		trackNewProcess(process);//TODO: dont create the FD table whichh will only get replaced now
	//TODO: do not error or child process already
	auto proc = processToInfo.find(process);
	auto val = processToInfo.find(parent);
	assert(val != processToInfo.end());
	assert(proc != processToInfo.end());

	if (!copy) {
		proc->second.fdTable = val->second.fdTable;
	}
	else {
		auto tbl = std::make_unique<MiddleEndState::FD_Table>(*val->second.fdTable);
		proc->second.fdTable = tbl.get();
		FD_Tables.push_back(std::move(tbl));

	}

}

void MiddleEndState::createDirectory(pid_t process, absFilePath filename, relFilePath relativePath)
{
	auto val = processToInfo.find(process);
	assert(val != processToInfo.end());
	MiddleEndState::file_info* fileInfo;
	if (auto it = encounteredFilenames.find(filename); it != encounteredFilenames.end()) {
		it->second->accessibleAs.emplace(process, relativePath, 0, false);
		fileInfo = it->second.get();
		assert(!fileInfo->isCurrentlyOnTheDisk);//TODO: handle multiple delete and open calls
		fileInfo->type = file_info::dir;


	}
	else {
		auto ptr = std::unique_ptr<MiddleEndState::file_info>(
			new file_info{
				.realpath = filename,
				.accessibleAs = {decltype(std::declval<MiddleEndState::file_info>().accessibleAs)::value_type{process, relativePath, 0, false, val->second.workdir}},
				.wasEverCreated = true,
				.wasEverDeleted = false,
				.isCurrentlyOnTheDisk = true,
				.wasInitiallyOnTheDisk = false,
				.type = file_info::dir
			});
		fileInfo = ptr.get();
		encounteredFilenames.emplace(filename, std::move(ptr));
	}
	fileInfo->isCurrentlyOnTheDisk = true;
	fileInfo->wasEverCreated = true;

}

void MiddleEndState::removeDirectory(pid_t process, absFilePath filename)
{
	auto val = processToInfo.find(process);
	assert(val != processToInfo.end());

	if (auto it = encounteredFilenames.find(filename); it != encounteredFilenames.end()) {
		it->second->accessibleAs.emplace(process, filename, 0, false);
		assert(it->second->isCurrentlyOnTheDisk);
		assert(it->second->type == file_info::dir);
		it->second->wasEverDeleted = true;
		it->second->isCurrentlyOnTheDisk = false;
	}
	else {
		auto ptr = std::unique_ptr<MiddleEndState::file_info>(
			new file_info{
				.realpath = filename,
				.accessibleAs = {decltype(std::declval<MiddleEndState::file_info>().accessibleAs)::value_type{process, filename, 0, false, val->second.workdir}},
				.wasEverCreated = false,
				.wasEverDeleted = true,
				.isCurrentlyOnTheDisk = false,
				.wasInitiallyOnTheDisk = true,
				.type = file_info::dir,
			});
		encounteredFilenames.emplace(filename, std::move(ptr));
	}
}

void MiddleEndState::removeNonDirectory(pid_t process, absFilePath filename)
{
	auto val = processToInfo.find(process);
	assert(val != processToInfo.end());

	if (auto it = encounteredFilenames.find(filename); it != encounteredFilenames.end()) {
		it->second->accessibleAs.emplace(process, filename, 0, false);
		assert(it->second->isCurrentlyOnTheDisk);
		assert(it->second->type != file_info::dir);
		it->second->isCurrentlyOnTheDisk = false;
		it->second->wasEverDeleted = true;
	}
	else {
		auto ptr = std::unique_ptr<MiddleEndState::file_info>(
			new file_info{
				.realpath = filename,
				.accessibleAs = {decltype(std::declval<MiddleEndState::file_info>().accessibleAs)::value_type{process, filename, 0, false, val->second.workdir}},
				.wasEverCreated = false,
				.wasEverDeleted = true,
				.isCurrentlyOnTheDisk = false,
				.wasInitiallyOnTheDisk = true,
				.type = std::nullopt,
			});
		encounteredFilenames.emplace(filename, std::move(ptr));
	}
}

void MiddleEndState::openHandling(pid_t process, absFilePath filename, relFilePath relativePath, fileDescriptor fd, int flags, bool existed)
{
	auto val = processToInfo.find(process);
	assert(val != processToInfo.end());
	MiddleEndState::file_info* fileInfo;
	if (auto it = encounteredFilenames.find(filename); it != encounteredFilenames.end()) {
		it->second->accessibleAs.emplace(process, relativePath, flags, false);
		fileInfo = it->second.get();
		assert(fileInfo->isCurrentlyOnTheDisk == existed);//TODO: don't fstat if we know
		fileInfo->wasEverCreated = !existed;
	}
	else {
		auto ptr = std::unique_ptr<MiddleEndState::file_info>(
			new file_info{
				.realpath = filename,
				.accessibleAs = {decltype(std::declval<MiddleEndState::file_info>().accessibleAs)::value_type{process, relativePath, flags, false, val->second.workdir}},
				.wasEverCreated = !existed,
				.wasEverDeleted = false,
				.isCurrentlyOnTheDisk = true,
				.wasInitiallyOnTheDisk = existed,
				.type = file_info::file,
			});
		fileInfo = ptr.get();
		encounteredFilenames.emplace(filename, std::move(ptr));
	}
	val->second.fdTable->table.emplace(fd, fileInfo);
}

void MiddleEndState::execFile(pid_t process, absFilePath filename, relFilePath relativePath)//TODO: Binfmt_misc
{
	auto val = processToInfo.find(process);
	assert(val != processToInfo.end());

	MiddleEndState::file_info* fileInfo;
	if (auto it = encounteredFilenames.find(filename); it != encounteredFilenames.end()) {
		it->second->accessibleAs.emplace(process, relativePath, std::nullopt, true);
		fileInfo = it->second.get();
	}
	else {
		auto ptr = std::unique_ptr<MiddleEndState::file_info>(
			new file_info{
				.realpath = filename,
				.accessibleAs = {decltype(std::declval<MiddleEndState::file_info>().accessibleAs)::value_type{process, relativePath, std::nullopt, true, val->second.workdir}},
				.wasEverCreated = false,
				.wasEverDeleted = false,
				.isCurrentlyOnTheDisk = true,
				.wasInitiallyOnTheDisk = true,
				.type = file_info::file,
			});
			
		fileInfo = ptr.get();
		encounteredFilenames.emplace(filename, std::move(ptr));
		int fd = open(filename.c_str(), O_RDONLY);//todo wrap me in a unique_ptr
		assert(fd >= 0);
		if (auto str = tryReadShellbang(fd); str.has_value()) {//TODO: this is essentially cached for further calls. Consider detecting if a given file was written since it had been executed and if it were, this needs to be redone.
			absFilePath path{ str.value() };
			assert(path.is_absolute());//TODO: add support for a relative path and the resulting resolution.
			execFile(process,path,path);//TODO: a loop? How does the kernel handle that?
		}
		close(fd);
	}
	//does not have an associated file descriptor.
}

void MiddleEndState::closeFile(pid_t process, fileDescriptor fd)
{
	auto val = processToInfo.find(process);
	assert(val != processToInfo.end());
	val->second.fdTable->table.erase(fd);
}
/*
* basically a 
* newfd = oldFd
*/
void MiddleEndState::registerFdAlias(pid_t process, fileDescriptor newFd, fileDescriptor oldFD)
{
	auto val = processToInfo.find(process);
	assert(val != processToInfo.end());
	if (auto oldFile = val->second.fdTable->table.find(oldFD); oldFile != val->second.fdTable->table.end()) {
		auto& replaceMe = val->second.fdTable->table[newFd];
		replaceMe = oldFile->second;
	}
	else {
		assert(false);
	}
}

void MiddleEndState::toBeDeleted(pid_t process)
{
	auto val = processToInfo.find(process);
	assert(val != processToInfo.end());
	val->second.exiting = true;

}

void MiddleEndState::registerPipe(pid_t process,fileDescriptor pipes[2])
{
	auto val = processToInfo.find(process);
	assert(val != processToInfo.end());
	auto pipe = ++this->pipeCount;
	auto out = new file_info{ 
		.realpath= "pipe_read_" + std::to_string(pipe), 
		.wasEverCreated = false,
		.wasEverDeleted = false,
		.isCurrentlyOnTheDisk = false,
		.wasInitiallyOnTheDisk = false,
		.type= file_info::pipe 
	};
	nonFileDescriptors.emplace_back(out);
	val->second.fdTable->table.emplace(pipes[0], out);
	auto in = new file_info{ 
		.realpath = "pipe_write_" + std::to_string(pipe) ,
		.wasEverCreated = false,
		.wasEverDeleted = false,
		.isCurrentlyOnTheDisk = false,
		.wasInitiallyOnTheDisk = false,
		.type = file_info::pipe
	};
	nonFileDescriptors.emplace_back(in);
	val->second.fdTable->table.emplace(pipes[1], in);
}

void MiddleEndState::registerSocket(pid_t process, fileDescriptor socket)
{
	auto val = processToInfo.find(process);
	assert(val != processToInfo.end());
	auto socketNr = ++this->socketCount;
	auto file = new file_info{
		.realpath = "socket_" + std::to_string(socketNr),
		.wasEverCreated = false,
		.wasEverDeleted = false,
		.isCurrentlyOnTheDisk = false,
		.wasInitiallyOnTheDisk = false,
		.type = file_info::socket
	};
	nonFileDescriptors.emplace_back(file);

	val->second.fdTable->table.emplace(socket, file);
}

void MiddleEndState::registerSocket(pid_t process, fileDescriptor sockets[2])
{
	auto socketNr = ++this->socketCount;

	auto val = processToInfo.find(process);
	assert(val != processToInfo.end());
	{
		auto file = new file_info{
			.realpath = "socket_pair_1_" + std::to_string(socketNr),
			.wasEverCreated = false,
			.wasEverDeleted = false,
			.isCurrentlyOnTheDisk = false,
			.wasInitiallyOnTheDisk = false,
			.type = file_info::socket
		};
		nonFileDescriptors.emplace_back(file);
		val->second.fdTable->table.emplace(sockets[0], file);
	}
	{
		auto file = new file_info{
			.realpath = "socket_pair_2_" + std::to_string(socketNr),
			.wasEverCreated = false,
			.wasEverDeleted = false,
			.isCurrentlyOnTheDisk = false,
			.wasInitiallyOnTheDisk = false,
			.type = file_info::socket
		};
		nonFileDescriptors.emplace_back(file);
		val->second.fdTable->table.emplace(sockets[1], file);
	}
}

void MiddleEndState::registerProcessFD(pid_t process, pid_t otherProcess,  fileDescriptor procFD)
{
	auto val = processToInfo.find(process);
	assert(val != processToInfo.end());
	auto processNR = ++this->processFD;
	auto file = new file_info{
		.realpath = "process_" + std::to_string(otherProcess) +"_"+ std::to_string(processNR),
		.wasEverCreated = false,
		.wasEverDeleted = false,
		.isCurrentlyOnTheDisk = false,
		.wasInitiallyOnTheDisk = false,
		.type = file_info::process
	};
	nonFileDescriptors.emplace_back(file);

	val->second.fdTable->table.emplace(procFD, file);
}
