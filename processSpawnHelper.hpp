#pragma once
#include <unistd.h>
#include <memory>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <filesystem>
#include <linux/close_range.h>

#include "./toBeClosedFd.hpp"
#include <sys/wait.h>


inline void NULLOPTFUNCTION() {};


inline ToBeClosedFd readClosedPipe() {
	int pipefd[2];
	assert(pipe(pipefd) != -1);
	close(pipefd[0]);//I will not be reading that, who knows what will happen to it though.
	return pipefd[1];
}

inline ToBeClosedFd writeClosedPipe() {
	int pipefd[2];
	assert(pipe(pipefd) != -1);
	close(pipefd[1]);//write EOF.
	return pipefd[0];
}

inline void execFail(int error) {
	switch (error) {
	case E2BIG: fprintf(stderr,"Too many args"); break;
	case EACCES: fprintf(stderr, "  Search permission is denied on a component of the path prefix of filename or the name of a script interpreter. \n"
		"| The file or a script interpreter is not a regular file. \n"
		"| Execute permission is denied for the file or a script or ELF interpreter.  \n"
		"| The file system is mounted noexec. \n"); break;
	case EFAULT: fprintf(stderr, "Filename points outside your accessible address space. "); break;
	case EINVAL: fprintf(stderr, "An ELF executable had more than one PT_INTERP segment (i.e., tried to name more than one interpreter). "); break;
	case EIO: fprintf(stderr,"An I/O error occurred. "); break;
	case EISDIR: fprintf(stderr, "An ELF interpreter was a directory. "); break;
	case ELIBBAD: fprintf(stderr, "An ELF interpreter was not in a recognized format. "); break;
	case ELOOP: fprintf(stderr, "Too many symbolic links were encountered in resolving filename or the name of a script or ELF interpreter. "); break;
	case EMFILE: fprintf(stderr, "The process has the maximum number of files open. "); break;
	case ENAMETOOLONG: fprintf(stderr, "Filename too long. "); break;
	case ENFILE: fprintf(stderr, "The system limit on the total number of open files has been reached. . "); break;
	case ENOENT: fprintf(stderr, "The file filename or a script or ELF interpreter does not exist, or a shared library needed for file or interpreter cannot be found. "); break;
	case ENOEXEC: fprintf(stderr, "An executable is not in a recognized format, is for the wrong architecture, or has some other format error that means it cannot be executed. "); break;
	case ENOMEM: fprintf(stderr, "Insufficient kernel memory was available."); break;
	case ENOTDIR: fprintf(stderr, "A component of the path prefix of filename or a script or ELF interpreter is not a directory. "); break;
	case EPERM: fprintf(stderr, "  The file system is mounted nosuid, the user is not the superuser, and the file has the set-user-ID or set-group-ID bit set \n"
		"| The process is being traced, the user is not the superuser and the file has the set - user - ID or set - group - ID bit set.  "); break;
	case ETXTBSY: fprintf(stderr, "Executable was open for writing by one or more processes."); break;
	default: fprintf(stderr, "unknown error %d", error); break;
	}

}

typedef struct {
	const char* filename;
	const size_t argc;
	char* const* argv;
} clone_args;

template<class Callback>
inline pid_t spawnProcess(int inFD, int outFD, int errFD, const char* programPath, char* const argv[], Callback callback) {

	pid_t pid = fork();
	if (pid != 0) { //master
		/*auto temp = argv;
		while (*temp != nullptr) {
			printf("arg: %s\n\n\n", *temp);
			++temp;
		}*/
		return pid;
	}
	else { //I am the child
		/**
		The child inherits copies of the parent's set of open file
		  descriptors.

		  but the table should differ

		This could in theory also be noe with clone and setting of correct flags.
		*/
		//These could be realistically constants.
		int in = fileno(stdin);
		int out = fileno(stdout);
		int err = fileno(stderr);

		auto tempErr = dup(err);

		dup2(inFD, in);
		dup2(outFD, out);
		dup2(errFD, err);
		// in-out == 0-2
		close_range(3, ~0U, CLOSE_RANGE_CLOEXEC);

		callback();
		int ret = execvp(programPath, argv);//return error if any, otherwise the child thread will return the sub-program's error. WE SEARCH PATH HERE!
		int error = errno;
		dup2(tempErr, err);
		fprintf(stderr, "Execv failed with errorcode %d :\n", error);
		execFail(error);

		exit(255);//we cannot be allowed to return from here.
	}
};

template<class Callback>
inline pid_t spawnProcessWithSimpleArgs(int inFd, int outFD, int errFD, const std::filesystem::path& programPath, char* const argv[], size_t argc, Callback callback) {
	if (argc < 1) {
		argc = 0; //and implicitly argv should point to nullptr, but I will not be checking that, for all I care argv can be nullptr.
	}
	//setup args arrays.
	clone_args args{
		.filename = programPath.c_str(),
		.argc = argc + 1,//arg count + filename
		.argv = nullptr,
	};

	// this requires fork not clone be used. Otherwise we do not know when they shall be freed.
	std::unique_ptr<char[]> filename = nullptr;
	{
		//no other way to get a mutable char*, only ever get to a const char *
		auto sv = std::string_view(programPath.filename().native());
		filename = std::make_unique<char[]>(sv.size() + 1);
		memcpy(filename.get(), sv.data(), sv.size());
		filename.get()[sv.size()] = 0; //ensure null terminated, may end up with two null terminators, better than 0
	}



	auto newArgv = std::make_unique<char* []>(args.argc + 1);
	newArgv[args.argc] = nullptr;
	newArgv[0] = filename.get();
	for (int i = 0; i < argc; ++i) {
		newArgv[i + 1] = argv[i];
	}
	args.argv = newArgv.get();

	auto ret = spawnProcess(inFd, outFD, errFD, args.filename, args.argv, callback);
	return ret;

};


inline std::optional<int> waitForTermination(pid_t pid) {
	int status;
	do {
		auto returnStatus = waitpid(pid, &status, 0);//TODO: wrap me in an object, just in case of exceptions
		if (returnStatus == -1) {
			int error = errno;
			switch (error) {
			case ECHILD:
				fprintf(stderr, "Waitpid The process specified by pid does not exist or is not a child of the calling process, or the process group specified by pid does not exist or does not have any member process that is a child of the calling process. \n");
				break;
			case EINTR:
				fprintf(stderr, "Waitpid The function was interrupted by a signal. The value of the location pointed to by stat_loc is undefined.\n");
				break;
			case EINVAL:
				fprintf(stderr, "The options argument is not valid.\n");
				break;
			default:
				fprintf(stderr, "Waitpid unknown error %d\n", error);
				break;
			}
			break;
		}
		else {
			assert(returnStatus == pid);//this could change in the case of catching the wait of another pid.
		}
	} while (!WIFEXITED(status) && !WIFSIGNALED(status));


	if (WIFSIGNALED(status)) {
		return std::nullopt;
	}
	else /*if (WIFEXITED(status))*/ {
		return WEXITSTATUS(status);
	}
}

class toBeClosedPid : public ToBeClosedGeneric<pid_t> {

	void tryClose() noexcept override {
		if (fd >= 0) {
			waitResult = waitForTermination(fd);
		}
	}
	public:
		std::optional<int> waitResult = std::nullopt;

		toBeClosedPid(pid_t pid) :ToBeClosedGeneric(pid) {}
};

struct spawnedValues {
	toBeClosedPid pid; //ORDER MATTERS HERE! we first close the FDs and then wait for termination. Do not reorder unles defining your own destructor with explicit reset calls
	ToBeClosedFd stderrFD;
	ToBeClosedFd stdoutFD;

	std::optional<int> close() {
		stdoutFD.reset();
		stderrFD.reset();
		pid.reset();
		return pid.waitResult;
	}
};



template<class Callback = decltype(NULLOPTFUNCTION)*>
inline spawnedValues spawnReadOnlyProcess(const std::filesystem::path& programPath, char* const argv[], size_t argc, Callback&& callback = NULLOPTFUNCTION) {
	int errfd[2];
	assert(pipe(errfd) != -1);
	//auto errfd = dup(fileno(stderr));
	int outfd[2];
	assert(pipe(outfd) != -1);

	auto pipe = readClosedPipe();
	auto res = spawnProcessWithSimpleArgs(pipe.get(), outfd[1], /*errfd */errfd[1], programPath, argv, argc, callback);
	close(outfd[1]);
	close(errfd[1]);
	return {
		.pid = res,
		.stderrFD = errfd[0],
		.stdoutFD = outfd[0],
	};

}


inline std::unique_ptr<char[]> fromConstCharToCharPtr(const std::string_view charData) {
	std::unique_ptr<char[]> retval{ new char[charData.size() + 1] };
	memcpy(retval.get(), charData.data(), charData.size());
	retval.get()[charData.size()] = 0;
	return retval;
}

template<std::convertible_to<std::string_view> ...Types>
std::unique_ptr<std::unique_ptr<char[]>[]> fromConstCharArrayToCharPtrArray(Types&& ...params ) {
	std::unique_ptr<std::unique_ptr<char[]>[]> buffer{ new std::unique_ptr<char[]>[sizeof...(params)] };
	size_t iter = 0;
	for (decltype(auto) type : std::initializer_list<std::string_view>{ params... }) {
		buffer.get()[iter] = std::move(fromConstCharToCharPtr(type));
		++iter;
	}
	return buffer;
}