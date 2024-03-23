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
		auto in = new file_info{ "stdin" };
		nonFileDescriptors.emplace_back(in);
		rawPtr->table.emplace(0, in);
		auto& out = nonFileDescriptors.emplace_back(new file_info{ "stdout" });
		rawPtr->table.emplace(1, out.get());
		auto& err = nonFileDescriptors.emplace_back(new file_info{ "stderr" });
		rawPtr->table.emplace(2, err.get());
	}
}

void MiddleEndState::trackNewProcess(pid_t process, pid_t parent, bool copy)
{
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

void MiddleEndState::openHandling(pid_t process, absFilePath filename, relFilePath relativePath, fileDescriptor fd, int flags, bool existed)
{
	auto val = processToInfo.find(process);
	assert(val != processToInfo.end());
	MiddleEndState::file_info* fileInfo;
	if (auto it = encounteredFilenames.find(filename); it != encounteredFilenames.end()) {
		it->second->accessibleAs.emplace(process, relativePath, flags, false);
		fileInfo = it->second.get();
	}
	else {
		auto ptr = std::make_unique<MiddleEndState::file_info>(filename, decltype(std::declval<MiddleEndState::file_info>().accessibleAs){ {process, relativePath, flags, false, val->second.workdir} });
		fileInfo = ptr.get();
		encounteredFilenames.emplace(filename, std::move(ptr));
	}
	fileInfo->wasCreated = fileInfo->wasCreated || (!existed);
	val->second.fdTable->table.emplace(fd, fileInfo);
}

void MiddleEndState::execFile(pid_t process, absFilePath filename, relFilePath relativePath)//TODO: Binfmt_misc
{
	auto val = processToInfo.find(process);
	assert(val != processToInfo.end());

	MiddleEndState::file_info* fileInfo;
	if (auto it = encounteredFilenames.find(filename); it != encounteredFilenames.end()) {
		it->second->accessibleAs.emplace(process, relativePath, 0, true);
		fileInfo = it->second.get();
	}
	else {
		auto ptr = std::make_unique<MiddleEndState::file_info>(filename, decltype(std::declval<MiddleEndState::file_info>().accessibleAs){ {process, relativePath, 0, true, val->second.workdir} });
		fileInfo = ptr.get();
		encounteredFilenames.emplace(filename, std::move(ptr));
		int fd = open(filename.c_str(), O_RDONLY);//todo wrap me in a unique_ptr
		assert(fd >= 0);
		if (auto str = tryReadShellbang(fd); str.has_value()) {
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
		//assert(false); TODO: log me
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
	auto out = new file_info{ "pipe_read_"+ std::to_string(pipe) };
	nonFileDescriptors.emplace_back(out);
	val->second.fdTable->table.emplace(pipes[0], out);
	auto in = new file_info{ "pipe_write_" + std::to_string(pipe) };
	nonFileDescriptors.emplace_back(in);
	val->second.fdTable->table.emplace(pipes[1], in);
}

std::optional<std::string_view> MiddleEndState::getFilePath(pid_t process, fileDescriptor fd) const
{
	auto val = processToInfo.find(process);
	assert(val != processToInfo.end());
	if (auto file = val->second.fdTable->table.find(fd); file != val->second.fdTable->table.end()) {
		return std::string_view{ file->second->realpath.c_str()};
	}
	return std::nullopt;
}

