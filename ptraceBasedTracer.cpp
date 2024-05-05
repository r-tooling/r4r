#include "ptraceBasedTracer.hpp"
#include "frontend/ptraceMainLoop.hpp"
#include "processSpawnHelper.hpp"
#include "toBeClosedFd.hpp"
#include "./cFileOptHelpers.hpp"
#include "backend/backEnd.hpp"

#include <cassert>

#include <sys/ptrace.h>
#include <unistd.h>
#include <cstddef>

#include <cstdio>
#include <cerrno>

#include <fcntl.h>//open, close...


using std::size_t;

#include <sys/wait.h>
#ifndef _GNU_SOURCE 
#define _GNU_SOURCE 1
#endif // !_GNU_SOURCE 

#include <sched.h>

namespace {

	void fileOpenFail(int err) noexcept {
		switch (err) {
		case EACCES: fprintf(stderr, "| The requested access to the file is not allowed, or search permission is denied for one of the directories in the"
			"path prefix of pathname, or the file did not exist yet and "
			"write access to the parent directory is not allowed. \n"
			"| Where O_CREAT is specified, the protected_fifos or "
			"protected_regular sysctl is enabled, the file already "
			"exists and is a FIFO or regular file, the owner of the "
			"file is neither the current user nor the owner of the "
			"containing directory, and the containing directory is both "
			"world - or group - writable and sticky.\n"); break;
		default: fprintf(stderr, "unknown error %d", err); break;
		}
	}

	void printUsage(const char* argv0) {
		printf("This tracer is used for analysing the dependencies of other computer programs. Usage:\n"
			"%s [programName] <argsToProgram>", argv0);
	}
}

int main(int argc, char* argv[])
{

	if (argc < 2) {
		printUsage(argc == 1 ? argv[0] : "ptraceBasedTracer");
		return -2;
	}

	assert(argc >= 2);//TODO: check args better.
	{
		int in = fileno(stdin);
		int out = fileno(stdout);
		int err = fileno(stderr);

		assert(in >= 0 && in < 3);//should be true anywhere.
		assert(err >= 0 && err < 3);
		assert(out >= 0 && out < 3);
	}

	/*
		redirect the ran program outputs to predefined files.
	*/

	ToBeClosedFd outPipe{ open("stdout.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IWUSR) };
	auto err = errno;
	if (outPipe.get() == -1) {
		fileOpenFail(err);
		return -1;
	}
	ToBeClosedFd errPipe{ open("stderr.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IWUSR) };
	err = errno;
	if (errPipe.get() == -1) {
		fileOpenFail(errno);
		return -1;
	}
	auto inPipe = writeClosedPipe();
	auto Tofree = FreeUniquePtr{ get_current_dir_name() };
	std::vector<std::string> args = {};
	for (auto it = 1; it < argc; ++it) {
		args.emplace_back(argv[it]);
	}

	middleend::MiddleEndState state{ Tofree.get(),environ,args };
	Tofree.reset();



	auto callback = []()noexcept {
		while (ptrace(PTRACE_TRACEME, 0, 0, 0) != -1);//wait utill parent is ready.  On error, all requests return -1. This should error out when parent is already attatched.
		raise(SIGTRAP);
		};

	pid_t childPid = spawnProcessWithSimpleArgs(inPipe.get(), outPipe.get(), errPipe.get(), argv[1], argv + 2, argc - 2, callback);//arg 1 == my filename, arg 2 == his filename
	//will recieve sigchild when it terminates and thus we can safely free the stack when taht happens. 
	//Though that should really be only done at the termination of main as otherwise we could get confused by getting sigchaild called from any other source. 
	// so not waiting for this PID is intentional.
	(void)childPid;
	frontend::ptraceChildren(state);
	backend::csvBased(state, "accessedFiles.csv");

	wait(nullptr);
	return 0;
}
