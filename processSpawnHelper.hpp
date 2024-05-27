#pragma once
#include <unistd.h>
#include <memory>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <filesystem>
#include <linux/close_range.h>
#include <variant>

#include "./backend/optionals.hpp"
#include "./toBeClosedFd.hpp"
#include "./cFileOptHelpers.hpp"
#include <fcntl.h>
#include <sys/wait.h>

namespace detail {
	/*
		Helper function to be used as a decltype argument, easier to understand than a typedef.
	*/
	inline void NULLOPTFUNCTION() noexcept {};
	/**
		Creates a new array of char * from an array of unique_ptrs to char *
	*/
	inline std::unique_ptr<char*> unpackArray(size_t nrItems, std::unique_ptr<char[]>* param) {
		std::unique_ptr<char*> result{ new char* [nrItems + 1] };
		for (size_t iter = 0; iter < nrItems; iter++) {
			result.get()[iter] = param[iter].get();
		}
		result.get()[nrItems] = nullptr;
		return result;
	}
	/*
		Used for creating a mutable zero terminated char[] from immutable const char*
		
	*/
	inline std::unique_ptr<char[]> fromConstCharToCharPtr(const std::string_view charData) {
		auto retval = std::make_unique_for_overwrite<char[]>(charData.size() + 1);
		memcpy(retval.get(), charData.data(), charData.size());
		retval.get()[charData.size()] = 0;
		return retval;
	}
	/*
	* A container of stringviews
	*/
	template<class T>
	concept ContainerContainingSV = requires(T x) {
		x.begin();
		x.end();
		x.size();
		{ *(x.begin()) } -> std::convertible_to<std::string_view>;
	};
	template<class Type>
	concept stringView_OrContainerOfStringView = std::is_convertible_v<Type, std::string_view> || ContainerContainingSV<Type>;
	/*
	* Overloads for getting the number of string views in a generic argument
	*/
	template<std::convertible_to<std::string_view> Item>
	constexpr size_t ItemsCount(Item&&) { return 1; };
	template<ContainerContainingSV Item>
	constexpr size_t ItemsCount(Item&& item) { return item.size(); };
	/*
		How many string views does a list of string views or containers of string views contain?
	*/
	template<stringView_OrContainerOfStringView ...Types>
	constexpr size_t CountAll(Types&&...params) { return (0 + ... + ItemsCount(params)); };
	/*
	* Creates a mutable copy of a string view list and assignes to an output array.
	*/
	template<std::convertible_to<std::string_view> Item>
	constexpr void assignPtr(std::unique_ptr<char[]>*& ptr, Item&& item) {
		*ptr = std::move(fromConstCharToCharPtr(item));
		++ptr;
	};
	template<ContainerContainingSV Item>
	constexpr void assignPtr(std::unique_ptr<char[]>*& ptr, Item&& arr) {
		for (decltype(auto) convertibleItem : arr) {
			*ptr = std::move(fromConstCharToCharPtr(convertibleItem));
			++ptr;
		}
	};
	/*
		Take a list of just about any argument which could then be turned into a string view or a range of string views. 
		Turn those into a mutable char* array.
	*/
	template<stringView_OrContainerOfStringView ...Types>
	inline std::unique_ptr<std::unique_ptr<char[]>[]> fromConstCharArrayToCharPtrArray(Types&& ...params) {
		size_t argc = CountAll(params...);
		auto buffer = std::make_unique_for_overwrite<std::unique_ptr<char[]>[]>(argc);
		auto ptr = buffer.get();
		(assignPtr(ptr, params), ...);
		return buffer;
	}
}
/*
* Used for specifying arguments of a system call in an ergonomic way. Does not preppend the program name
* Allocates new memory for the wrapped arguments.
* Can be created with just about anything convertible to string_view or a a range of string_views.
*/
struct ArgvWrapper {

	const size_t argc;
	const std::unique_ptr<std::unique_ptr<char[]>[]> uniquePtrArr;
	const std::unique_ptr<char*> uniqueToSimple;
	auto get() noexcept{
		return uniqueToSimple.get();
	}

	template<detail::stringView_OrContainerOfStringView ...Types>
	ArgvWrapper(Types&& ...params)
		:argc{ detail::CountAll(params...) },
		uniquePtrArr{ detail::fromConstCharArrayToCharPtrArray(params...)},
		uniqueToSimple{ detail::unpackArray(argc,uniquePtrArr.get()) }
	{}

};
//TODO: gracefully ahndle the asserts in this file.

/*
	Create a pipe which can be written but the output will be discarded.
*/
inline ToBeClosedFd readClosedPipe() {
	int pipefd[2];
	auto pipe_res = pipe(pipefd);
	assert(pipe_res != -1);
	close(pipefd[0]);//I will not be reading that, who knows what will happen to it though.
	(void)pipe_res;
	return ToBeClosedFd{ pipefd[1] };
}
/*
	Create a pipe which can only be read from but will never be written.
	Thus reads EOF but is a valid FD.
*/
inline ToBeClosedFd writeClosedPipe() {
	int pipefd[2];
	auto pipe_res = pipe(pipefd);
	assert(pipe_res != -1);
	close(pipefd[1]);//write EOF.
	(void)pipe_res;
	return ToBeClosedFd{ pipefd[0] };
}
/*
* Exec fail nice messages.
*/
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


/*
Spawns a new process using fork - so the arguments can be freed right after the call.
If the exec fails, the child is not allowed to return and exits.
The child invokes the callback right before spawing with valid file descriptors marked for close on exec - can be used to for example revert some of the automated markings.
*/
template<class Callback>
inline pid_t spawnProcess(int inFD, int outFD, int errFD, const char* programPath, char* const argv[], Callback callback) noexcept {
	static_assert(noexcept(callback()));
	pid_t pid = fork();
	if (pid != 0) { //master
		assert(pid > 0);//TODO: error handling cannot fork.
		return pid;
	}
	else { //I am the child
		/**
		The child inherits copies of the parent's set of open file
		  descriptors.

		  but the table should differ

		This could in theory also be noe with clone and setting of correct flags.
		*/
		//These could be realistically constants but I doubt the overhead matters.
		int in = fileno(stdin);
		int out = fileno(stdout);
		int err = fileno(stderr);

		auto tempErr = dup(err);

		dup2(inFD, in);
		dup2(outFD, out);
		dup2(errFD, err);
		// in-out == 0-2 - had been asserted in main();
		close_range(3, ~0U, CLOSE_RANGE_CLOEXEC);

		callback();
		execvp(programPath, argv);//return error if any, otherwise the child thread will return the sub-program's error. WE SEARCH PATH HERE!
		int error = errno;
		dup2(tempErr, err); //restore old stderr to valid state.
		fprintf(stderr, "Execv failed with errorcode %d :\n", error);
		execFail(error);

		exit(255);//we cannot be allowed to return from here.
	}
};
/*
* A simple wrapper for all the arguments a clone call needs.
*/
struct clone_args {
	const char* filename;
	const size_t argc;
	char* const* argv;
};
/*
* Spawns a process with argc error handling and pre-pending the filename to the argv array
*/
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
	for (size_t i = 0; i < argc; ++i) {
		newArgv[i + 1] = argv[i];
	}
	args.argv = newArgv.get();

	auto ret = spawnProcess(inFd, outFD, errFD, args.filename, args.argv, callback);
	return ret;

};

struct CodeReturn{
	int code;
};
struct SignalReturn {
	int signal;
};

using ReturnState = std::variant<CodeReturn, SignalReturn>;
/*
	Wait untill a child process specified by pid finishes execution. 
*/
inline ReturnState waitForTermination(pid_t pid) {
	int status;
	do {
		auto returnStatus = waitpid(pid, &status, 0);
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
			assert(returnStatus == pid);//this could change in the case of catching the wait of another pid. THIS SHOULD BE IMPOSSIBLE DUE TO THE NATURE OF THE CALL
		}
	} while (!WIFEXITED(status) && !WIFSIGNALED(status));


	if (WIFSIGNALED(status)) {
		//fprintf(stderr, "Direct child process terminated by signal with status %d\n", WTERMSIG(status));
		return SignalReturn{ WTERMSIG(status) };
	}
	else /*if (WIFEXITED(status))*/ {
		return CodeReturn{ WEXITSTATUS(status) };
	}
}

struct PidDeleter {
	constexpr static pid_t invalidValue = -1;
	std::optional<ReturnState> waitResult;

	void operator()(pid_t& pid) {
		if (pid >= 0) {
			waitResult = waitForTermination(pid);
			pid = -1;
		}
	}
	constexpr bool is_valid(const pid_t& fd) const noexcept {
		return fd >= 0;
	}
};
/*
	A helper which will wait for a process to exit when going out of scope.
	Care, since assinging a new pid will not purge the last wait result.
*/
using toBeClosedPid = ToBeClosedGeneric<pid_t, PidDeleter>;
/*
All the relevant parts of a spawned process wrapped in a class
*/
struct [[nodiscard]] SpawnedValues  {
	toBeClosedPid pid; //ORDER MATTERS HERE! we first close the FDs and then wait for termination. Do not reorder unles defining your own destructor with explicit reset calls
	ToBeClosedFd stderrFD;
	ToBeClosedFd stdoutFD;

	std::optional<ReturnState> close() {
		stdoutFD.reset();
		stderrFD.reset();
		pid.reset();
		return pid.waitResult;
	}

	SpawnedValues(pid_t pid, ToBeClosedFd err, ToBeClosedFd out) :pid(pid), stderrFD(std::move(err)), stdoutFD(std::move(out)) {}
	SpawnedValues(pid_t pid, fileDescriptor err, fileDescriptor out) :pid(pid), stderrFD(std::move(err)), stdoutFD(std::move(out)) {}
};
struct ReturnStateWrapper {
	std::optional<ReturnState> processRes;
	bool terminatedCorrectly() {
		return processRes.has_value() && std::holds_alternative<CodeReturn>(processRes.value()) && std::get<CodeReturn>(processRes.value()).code == 0;
	}
};


/*
All the relevant parts of a spawned process wrapped in a class with the bonus of using C-style FILE*
It does not igev c++ streams as there is no method for creating a stream from a FILE* nor from a file descriptor.
*/
struct [[nodiscard]] SpawnedValuesFilePtr {
	toBeClosedPid pid; //ORDER MATTERS HERE! we first close the FDs and then wait for termination. Do not reorder unles defining your own destructor with explicit reset calls
	ToBeClosedFileFD err;
	ToBeClosedFileFD out;

	ReturnStateWrapper close() {
		out.reset();
		err.reset();
		pid.reset();
		return { pid.waitResult };
	}

	SpawnedValuesFilePtr(pid_t pid, fileDescriptor err, fileDescriptor out) :pid(pid), err(ToBeClosedFd{ err }, "r"), out(ToBeClosedFd{ out }, "r") {}
};

template<typename T>
concept ArgvWrapperLike = requires{
	std::same_as<std::decay_t<ArgvWrapper>, T>;
};

/*
* A high level function for spawning a process with file descriptor returns.
*/
template<class Callback = decltype(detail::NULLOPTFUNCTION)*, ArgvWrapperLike argv_t>
inline SpawnedValues spawnReadOnlyProcess(const std::filesystem::path& programPath, argv_t&& argv, Callback&& callback = detail::NULLOPTFUNCTION) {
	int errfd[2];
	auto pipe_res = pipe(errfd);
	assert(pipe_res != -1);
	
	int outfd[2];
	pipe_res = pipe(outfd);
	(void)pipe_res;
	assert(pipe_res != -1);

	auto pipe = readClosedPipe();
	auto pid = spawnProcessWithSimpleArgs(pipe.get(), outfd[1], /*errfd */errfd[1], programPath, argv.get(), argv.argc, callback);
	close(outfd[1]);
	close(errfd[1]);
	return {pid,errfd[0],outfd[0]};
}


/*
* A high level function for spawning a process which is only meant to write to stdout.
*/
template<class Callback = decltype(detail::NULLOPTFUNCTION)*, ArgvWrapperLike argv_t>
inline SpawnedValuesFilePtr spawnStdoutReadProcess(const std::filesystem::path& programPath, argv_t&& argv, Callback&& callback = detail::NULLOPTFUNCTION) {
	int errfd = open("/dev/null", O_WRONLY); 
	assert(errfd != -1);
	int outfd[2];
	auto pipe_res = pipe(outfd);
	(void)pipe_res;
	assert(pipe_res != -1);
	auto pipe = readClosedPipe();
	auto pid = spawnProcessWithSimpleArgs(pipe.get(), outfd[1], /*errfd */errfd, programPath, argv.get(), argv.argc, callback);
	close(outfd[1]);
	close(errfd);
	return { pid,-1,outfd[0] };
}

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template<ArgvWrapperLike argv_t>
inline bool checkExecutableExists(const std::filesystem::path& programPath, argv_t&& argv) {
	static auto codeRet = [](CodeReturn c) {return c.code; };
	static auto signalRet = [](SignalReturn c) {return c.signal == SIGPIPE ? 0 : -1; }; //sigpipe is bound to hapen as I just close the output immadietely. Could be improved to redirect to /dev/null.
	return optTransform(spawnStdoutReadProcess(programPath, argv).close().processRes, [](auto var) { return std::visit(overloaded{ codeRet,signalRet }, var); }).value_or(-1) == 0;
}