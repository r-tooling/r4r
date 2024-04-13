#pragma once
#include <string>
#include <tuple>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <memory>
#include <filesystem>
#include <optional>

#include <cassert>

typedef int fileDescriptor;
typedef std::filesystem::path absFilePath;
typedef std::filesystem::path relFilePath;

struct access_info {
	pid_t pid; //this will eventually be replaced with the state of the current acceess rights as in teh logged in user/groups etc.
	relFilePath relPath;
	std::optional<int> flags = std::nullopt; //sometimes the file is only created but never opened.
	bool executable;
	absFilePath workdir;
	auto operator<=>(access_info const& other) const noexcept {//cannt be default as string is annoying
		return std::tie(pid, relPath, flags, executable, workdir) <=> std::tie(other.pid, other.relPath, other.flags, executable, workdir);
	}
	bool operator==(access_info const& other) const noexcept {
		return std::tie(pid, relPath, flags, executable, workdir) == std::tie(other.pid, other.relPath, other.flags, executable, workdir);
	}
};

template<>
struct std::hash<access_info> {
	std::size_t operator()(access_info const& s) const noexcept {
		std::size_t h1 = std::hash<std::string>{}(s.relPath);
		std::size_t h2 = std::hash<pid_t>{}(s.pid) << 2;
		std::size_t h3 = std::hash<pid_t>{}(s.flags.value_or(0)) << 6;
		return  h1 ^ h2 ^ h3;
	}
};

class MiddleEndState {
public:
	struct file_info {
		absFilePath realpath;
		std::unordered_set<access_info> accessibleAs; //but we would need to know where it is relative from right now... Assuming programs do not move their current directory.

		std::optional<bool> wasEverCreated;
		std::optional<bool> wasEverDeleted;
		std::optional<bool> isCurrentlyOnTheDisk;
		const std::optional<bool> wasInitiallyOnTheDisk;

		enum FileType { file, pipe, socket, process, dir,
		blockDev,  //TODO: use me
		charDev, //TODO: use me
		link, //TODO: use me
		clock, //timerfd_create
		epoll,
		eventFD,
		}; //may not be set for a pure unlink() call.
		std::optional<FileType> type = std::nullopt;
		//different required access rights?
		

		void registerAccess(access_info&& access) {
			accessibleAs.emplace(access);
		}

		//TODO: consider supporting hard links via saving the inode number and matching that. 
	};

	struct FD_Table {
		std::unordered_map<fileDescriptor, file_info*> table;
	};


	struct FS_Info {
		absFilePath workdir;
		absFilePath chroot = "/";
		//umask
	};

private:

	bool firstProcessInitialised = false;

	size_t processFD = 0;
	size_t pipeCount = 0;
	size_t socketCount = 0;
	size_t timerCount = 0;
	size_t epollCount = 0;
	size_t eventCount = 0;


	size_t errorCount = 0;

	//Todo: handle hard links. I dont even know where I'd begin with the detection, though. I would need to keep track of inode numbers and their potential deletions. Meaning I'd have to add inode watchers to the entire process.

	//current file descriptor state in each process PID. Need to support sharing between say threads.
	//todo: hnadle close, dup and other calls which manipulate file descriptors.
	struct running_thread_state {
		const pid_t pid;
		std::shared_ptr<FD_Table> fdTable; //cannot be const as unshare exists.
		std::shared_ptr<FS_Info> fsInfo;  
		bool exiting = false; //just in case a child process decides to replace the PID of another child. In such cases a PID collision is quite alright.

		running_thread_state() = delete;

		running_thread_state(pid_t pid, std::shared_ptr<FS_Info>&& fsInfo, std::shared_ptr<FD_Table>&& fdTable) : pid(pid), fsInfo(fsInfo), fdTable(fdTable) {}
		running_thread_state(const running_thread_state&) = delete;
		running_thread_state(running_thread_state&& other) noexcept = default;
		
		auto operator<=>(running_thread_state const& other) const noexcept {
			return pid <=> other.pid;

		};
		bool operator==(access_info const& other) const noexcept {
			return pid == other.pid;
		}

		void registerFD(fileDescriptor fd, file_info* info) {
			fdTable->table.insert_or_assign(fd, info);
		}
	};
	std::unordered_map < pid_t, running_thread_state > processToInfo;

	std::vector<std::unique_ptr<file_info>> nonFileDescriptors;

	MiddleEndState::file_info* createUnbackedFD(absFilePath&& filename, MiddleEndState::file_info::FileType type);
	MiddleEndState::file_info* createErrorFD(const char* errorMessage = "operating on an unresolved file descriptor\n");
	MiddleEndState::file_info* tryFindFile(const absFilePath& filename);
	running_thread_state& pidToObj(const pid_t process);

public:
	MiddleEndState() = default;
	MiddleEndState(const MiddleEndState&) = delete;
	MiddleEndState(MiddleEndState&&) = delete;


	std::unordered_map<absFilePath, std::unique_ptr<file_info>> encounteredFilenames;

	void trackNewProcess(pid_t process);//TODO: support sharing FDs by copying a FD table reference and inherting a different workdir
	void trackNewProcess(pid_t process,pid_t parent, bool copy,std::optional<pid_t> assumedChildPid, bool cloneFS);
	void createDirectory(pid_t process,const absFilePath& filename,const relFilePath& relativePath);
	void removeDirectory(pid_t process, absFilePath filename);

	void removeNonDirectory(pid_t process, absFilePath filename);


	void changeDirectory(pid_t process, std::filesystem::path newWorkingDirectory);
	void changeDirectory(pid_t process, fileDescriptor fileDescriptor);
	std::filesystem::path resolveToAbsoltute(pid_t process, const std::filesystem::path& relativePath, fileDescriptor fileDescriptor);
	std::filesystem::path resolveToAbsoltute(pid_t process,const std::filesystem::path& relativePath);
	//for paths after calling unlink and such.
	std::filesystem::path resolveToAbsoltuteDeleted(pid_t process, const std::filesystem::path& relativePath);



	void openHandling(pid_t process, absFilePath filename, relFilePath relativePath, fileDescriptor fd, int flags, bool existed);
	//TODO: Handle CloseOnExec
	/*
	* @returns bool whether to expect a return from the syscall or not
	*/
	bool execFile(pid_t process, absFilePath filename, relFilePath relativePath,size_t depth = 0, bool override = false);
	void closeFileDescriptor(pid_t process, fileDescriptor fd);
	void listDirectory(pid_t process, fileDescriptor fd);
	
	void registerFdAlias(pid_t process, fileDescriptor newFd, fileDescriptor oldFD);
	void toBeDeleted(pid_t process);
	void registerPipe(pid_t process, fileDescriptor pipes[2]);
	void registerSocket(pid_t process, fileDescriptor socket);
	void registerSocket(pid_t process, fileDescriptor sockets[2]);

	void registerProcessFD(pid_t process, pid_t otherProcess, fileDescriptor procFD);
	void registerTimer(pid_t process, fileDescriptor timerFD);
	void registerEpoll(pid_t process, fileDescriptor epollFD);
	
	void registerEventFD(pid_t process, fileDescriptor eventFD);

	/*
	* lifetime guaranteed only untill a directory change or the process terination.
	*/
	const std::filesystem::path& getCWD(pid_t process) const {
		auto val = processToInfo.find(process);
		assert(val != processToInfo.end());
		return val->second.fsInfo->workdir;
	}


	//lifetime only guaranteed utill the process lives. Or maybe not maybe the entire state, who knows.
	// here because I am too lazy to define a logging and a non-logging variant 
	template<bool log = true>
	std::optional<std::string_view> getFilePath(pid_t process, fileDescriptor fd) const {
		auto val = processToInfo.find(process);
		assert(val != processToInfo.end());
		if (auto file = val->second.fdTable->table.find(fd); file != val->second.fdTable->table.end()) {
			return std::string_view{ file->second->realpath.c_str() };
		}
		if constexpr (log) {
			fprintf(stderr, "Unable to resolve file descriptor %d\n", fd);
		}
		return std::nullopt;
	}
	;
};


