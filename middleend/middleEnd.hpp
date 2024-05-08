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
#include "../toBeClosedFD.hpp" //for fileDescriptor info

/*
* semantically an argument of this type should be an absolute path.
*/
typedef std::filesystem::path absFilePath;
/*
* semantically an argument of this type should be a relative path.
*/
typedef std::filesystem::path relFilePath;
namespace middleend {
	/*
		An individual attempt at accessing a file
	*/
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
}
//allow for the access info to be a part of hashmaps
template<>
struct std::hash<middleend::access_info> {
	std::size_t operator()(middleend::access_info const& s) const noexcept {
		std::size_t h1 = std::hash<std::string>{}(s.relPath);
		std::size_t h2 = std::hash<pid_t>{}(s.pid) << 2;
		std::size_t h3 = std::hash<pid_t>{}(s.flags.value_or(0)) << 6;
		return  h1 ^ h2 ^ h3;
	}
};

namespace middleend{
	/*
		The actual kernel emulator and logger.
	*/
	class MiddleEndState {
	public:
		std::vector<std::string> args;
		std::vector<std::string> env;
		std::string initialDir;

		/*
			A single file "cache" and our knowledge of it
			Ii is also used internally to store information about file descriptors of non files.
		*/
		struct file_info {
			absFilePath realpath; //real path
			std::unordered_set<access_info> accessibleAs; //but we would need to know where it is relative from right now... Assuming programs do not move their current directory.

			std::optional<bool> wasEverCreated;
			std::optional<bool> wasEverDeleted;
			std::optional<bool> isCurrentlyOnTheDisk;
			const std::optional<bool> wasInitiallyOnTheDisk;

			enum FileType {
				file, pipe, socket, process, dir,
				blockDev,  //TODO: use me
				charDev, //TODO: use me
				link, //TODO: use me
				clock, //timerfd_create
				epoll,
				eventFD,
			}; //may not be set for a pure unlink() call.
			std::optional<FileType> type = std::nullopt;
			std::optional<bool> requiresAllSubEntities; //this is currently only used for directory listing
			//different required access rights?


			void registerAccess(access_info&& access) {
				accessibleAs.emplace(access);
			}

			//TODO: consider supporting hard links via saving the inode number and matching that. 
			//this would first require filesystems support
		};
		/*
			The file descriptor table of a process
		*/
		struct FD_Table {
			std::unordered_map<fileDescriptor, file_info*> table;
		};
		/*
			The filesystem interaction data saved by the kernel of each process.
		*/
		struct FS_Info {
			absFilePath workdir;
			absFilePath chroot = "/";
			//umask
		};

	private:
		//used for  initialising the state of the first traced process.
		bool firstProcessInitialised = false;
		/*
			These are counters used for creating unique IDs for individual creations of the corresponding file descriptors.
		*/
		size_t processFD = 0;
		size_t pipeCount = 0;
		size_t socketCount = 0;
		size_t timerCount = 0;
		size_t epollCount = 0;
		size_t eventCount = 0;
		//used in the case that a never before seen fd is used
		size_t errorCount = 0;
		
		/*
			The information saved for each thread
		*/
		struct running_thread_state {
			//TODO: user IDs
			const pid_t pid;
			std::shared_ptr<FD_Table> fdTable; //cannot be const as unshare exists.
			std::shared_ptr<FS_Info> fsInfo;
			bool exiting = false; //just in case a child process decides to replace the PID of another child. In such cases a PID collision is quite alright. But otherwise the process is meant to still be running.

			running_thread_state() = delete;

			running_thread_state(pid_t pid, std::shared_ptr<FS_Info>&& fsInfo, std::shared_ptr<FD_Table>&& fdTable) : pid(pid), fdTable(fdTable), fsInfo(fsInfo) {}
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
		std::unordered_set < int> syscallWarnings;
		std::unordered_map < pid_t, running_thread_state > processToInfo;
		/*
		A list of all file descriptors to entities outside the filesystem.
		*/
		std::vector<std::unique_ptr<file_info>> nonFileDescriptors;
		/*
			Create a FD not on the filesystem
		*/
		MiddleEndState::file_info* createUnbackedFD(absFilePath&& filename, MiddleEndState::file_info::FileType type);
		/*
			errorus FD we know nothign of.
		*/
		MiddleEndState::file_info* createErrorFD(const char* errorMessage = "operating on an unresolved file descriptor\n");
		/*
			Attempts to find info of a file given by path.

		*/
		MiddleEndState::file_info* tryFindFile(const absFilePath& filename);
		running_thread_state& pidToObj(const pid_t process);
		const running_thread_state& pidToObj(const pid_t process) const;

	public:
		MiddleEndState() = delete;
		MiddleEndState(absFilePath initialWorkdir, char* initailEnv[], std::vector<std::string> initialCommand);
		MiddleEndState(const MiddleEndState&) = delete;
		MiddleEndState(MiddleEndState&&) = delete;

		/*
			A set of all the filenames interacted with
			TODO: as of now it does not contain information about files which should NOT exist at all.
		*/
		std::unordered_map<absFilePath, std::unique_ptr<file_info>> encounteredFilenames;

		/*
			Track a process where the parent is unknown - this shoud hapen very rarely if ever. I do not see a way in which a process can spawn without another first entering a clone call.
		*/
		void trackNewProcess(pid_t process);
		/*
			Track a process with a known CREATOR PID, this is different from the potential parent PID. 
			copy = copy or share the FD table
			assumedChildPid - is to tell the middle end if we potentially assigned this process a different child before.
			cloneFS = copy or share workdir and such
		*/
		void trackNewProcess(pid_t process, pid_t creator, bool copy, std::optional<pid_t> assumedChildPid, bool cloneFS);
		//Technically the process data will not get deleted as of yet due to possible race conditions. Will be deleted when a new process with the same TID gets created.
		void toBeDeleted(pid_t process);

		
		void createDirectory(pid_t process, const absFilePath& filename, const relFilePath& relativePath);
		//rmdir
		void removeDirectory(pid_t process, const absFilePath& filename);

		void listDirectory(pid_t process, fileDescriptor fd);


		void changeDirectory(pid_t process, const std::filesystem::path& newWorkingDirectory);
		void changeDirectory(pid_t process, fileDescriptor fileDescriptor);
		/*
			At explicit file descriptor
		*/
		absFilePath resolveToAbsolute(pid_t process, const std::filesystem::path& relativePath, fileDescriptor fileDescriptor, bool log = true) const;

		/*
			at FDCWD
		*/
		absFilePath resolveToAbsolute(pid_t process, const std::filesystem::path& relativePath, bool log = true) const;
		/*
			for paths after calling unlink and such. - the resolution algorithm will not fail on inexiistent directories
		*/
		absFilePath resolveToAbsoluteDeleted(pid_t process, const std::filesystem::path& relativePath) const;

	
		//Open file
		void openHandling(pid_t process, absFilePath filename, relFilePath relativePath, fileDescriptor fd, int flags, bool existed);
		
		/*
		* @returns bool whether to expect a return from the syscall or not
		*/
		bool execFile(pid_t process, absFilePath filename, relFilePath relativePath, size_t depth = 0, bool override = false);

		//unlink
		void removeNonDirectory(pid_t process, const absFilePath& filename);

		//remove a FD from the FD table
		void closeFileDescriptor(pid_t process, fileDescriptor fd);

		void registerFdAlias(pid_t process, fileDescriptor newFd, fileDescriptor oldFD);
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

		/*
			Check if a file exists for a given process witht he usual semantics of at<call>
			
		*/
		bool checkFileExists(pid_t process, fileDescriptor at, const relFilePath& fileRelPath, int flags) const;
		/*
			Add a warning about the usage of a syscall which may result in nondeterminism
		*/
		void syscallWarn(int nr, const char * message);
	};
}