

#include "ptraceBasedTracer.hpp"
#include "ptraceMainLoop.hpp"

#include <cassert>

#include <sys/ptrace.h>
#include <linux/close_range.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <cstddef>

#include <cstdio>
#include <cerrno>

#include <fcntl.h>//open, close...


using std::size_t;

#include <sys/wait.h>

#define _GNU_SOURCE   
#include <sched.h>
#undef _GNU_SOURCE   



void execFail(int error) {
	switch (error) {
	case E2BIG: printf("Too many args"); break;
	case EACCES: printf(" Search permission is denied on a component of the path prefix of filename or the name of a script interpreter. \n"
		"| The file or a script interpreter is not a regular file. \n"
		"| Execute permission is denied for the file or a script or ELF interpreter.  \n"
		"| The file system is mounted noexec. \n"); break;
	case EFAULT: printf("filename points outside your accessible address space. "); break;
	case EINVAL: printf("An ELF executable had more than one PT_INTERP segment (i.e., tried to name more than one interpreter). "); break;
	case EIO: printf(" An I/O error occurred. "); break;
	case EISDIR: printf(" An ELF interpreter was a directory. "); break;
	case ELIBBAD: printf("An ELF interpreter was not in a recognized format. "); break;
	case ELOOP: printf("Too many symbolic links were encountered in resolving filename or the name of a script or ELF interpreter. "); break;
	case EMFILE: printf("The process has the maximum number of files open. "); break;
	case ENAMETOOLONG: printf("Finelanme too long. "); break;
	case ENFILE: printf("The system limit on the total number of open files has been reached. . "); break;
	case ENOENT: printf("The file filename or a script or ELF interpreter does not exist, or a shared library needed for file or interpreter cannot be found. "); break;
	case ENOEXEC: printf("An executable is not in a recognized format, is for the wrong architecture, or has some other format error that means it cannot be executed. "); break;
	case ENOMEM: printf("Insufficient kernel memory was available."); break;
	case ENOTDIR: printf("A component of the path prefix of filename or a script or ELF interpreter is not a directory. "); break;
	case EPERM: printf("The file system is mounted nosuid, the user is not the superuser, and the file has the set-user-ID or set-group-ID bit set \n"
		"| The process is being traced, the user is not the superuser and the file has the set - user - ID or set - group - ID bit set.  "); break;
	case ETXTBSY: printf("Executable was open for writing by one or more processes."); break;
	default: printf("unknown error %d", error); break;
	}

}


void fileOpenFail(int err) {
	switch (err) {
	case EACCES: printf("| The requested access to the file is not allowed, or search permission is denied for one of the directories in the"
		"path prefix of pathname, or the file did not exist yet and "
		"write access to the parent directory is not allowed. \n"
		"| Where O_CREAT is specified, the protected_fifos or "
		"protected_regular sysctl is enabled, the file already "
		"exists and is a FIFO or regular file, the owner of the "
		"file is neither the current user nor the owner of the "
		"containing directory, and the containing directory is both "
		"world - or group - writable and sticky.\n"); break;
	default: printf("unknown error %d", err); break;
	}
}


typedef struct {
	char* filename;
	int argc;
	char** argv;
} clone_args;

static int childExec(void * arg){
	clone_args * actualArg = (clone_args*)arg;

/*
	printf("filename: %s\n", actualArg->filename);
	for (int i = 0; i < actualArg->argc; i++) {
		printf("arg %d %s\n", i, actualArg->argv[i]);
	}
	printf("Exiting\n");*/


	//int unshare(int flags);
	close_range(3, ~0U, CLOSE_RANGE_CLOEXEC);

	//just a check that EOF is truly read from stdin in the child.
	//const char ar[1]{};
	//assert(read(fileno(stdin), (void*)ar, 1) == 0);


	//TODO: log current environment somewhere.

	while(ptrace(PTRACE_TRACEME, 0, 0, 0) != -1);//wait utill parent is ready.  On error, all requests return -1. This should error out when parent is already attatched.
	raise(SIGTRAP);
	int ret = execv(actualArg->filename,actualArg->argv);//return error if any, otherwise the child thread will return the sub-program's error.
	int error = errno;
	//TODO: I might need to undo the stdout modifications here.
	printf("Execv failed with errorcode %d :", error);
	execFail(error);
	return ret;
}

#define STACK_SIZE (4024)    /* Stack size for cloned child, should be  more than enough considering it immadietely execs */

int main(int argc, char * argv[])
{
	if (argc == 1) {//./ptraceBasedTracer 
		const char* temp[] = { "ptraceBassedTracer", "/usr/bin/R", "-e", "q()",nullptr };
		argc = sizeof(temp) / (sizeof(*temp)) - 1;
		argv = (char**)malloc((argc + 1) * sizeof(char**));//hacks, remove me when you figure out how to do args in visual studio
		for (int a = 0; a < argc; a++) {
			argv[a] = (char*)malloc((strlen(temp[a]) + 1) * sizeof(*temp[a]));
			strcpy(argv[a], temp[a]);
		}
		argv[argc] = nullptr;
	}
	

	assert(argc >= 2);//TODO: check args better.
	
	int in = fileno(stdin);
	int out = fileno(stdout);
	int err = fileno(stderr);

	assert(in >= 0 && in < 3);//should be true anywhere.
	assert(err >= 0 && err < 3);
	assert(out >= 0 && out < 3);
	
	char *childStack = (char*)malloc(STACK_SIZE);
	
	int oldPipes[3];
	
	//save old fds so we may interact with them again.
	for(size_t i =0; i<3;++i ){
		assert((oldPipes[i] = dup(i)) != -1);
	}
	
	//create new file descriptors for the child.
	int newPipes[3];
	newPipes[out] = open("stdout.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IWUSR);
	if (newPipes[out] == -1) {
		fileOpenFail(errno);
		return -1;
	}
	newPipes[err] = open("stderr.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IWUSR);
	if (newPipes[err] == -1) {
		fileOpenFail(errno);
		return -1;
	}
	int pipefd[2];
	assert(pipe(pipefd) != -1);
	newPipes[in] = pipefd[0];
	close(pipefd[1]);//write EOF.
	
	//set FDS as the old ones
	dup2(newPipes[in], in);
	dup2(newPipes[out], out);
	dup2(newPipes[err], err);
	
	//setup args arrays.
	clone_args args;
	args.filename = argv[1];
	size_t len = strlen(args.filename);
	char * filename = (char*)malloc(sizeof(char*)* (len + 1));
	memcpy(filename, argv[1],len +1);
	char * newArgv[argc]; //var length array, deal with it.
	newArgv[argc-1] = nullptr;
	newArgv[0] = basename(filename);
	args.argc = argc -1;
	for(int i = 1; i < argc; ++i){
		newArgv[i] = argv[i +1];
	} 
	args.argv = newArgv;
	pid_t childPid = clone(childExec, childStack + STACK_SIZE, SIGCHLD, &args);//will recieve sigchild when it terminates and thus we can safely free the stack when taht happens. Though that should really be only done at the termination of main as otherwise we could get confused by getting sigchaild called from any other source. 
	
	//restore old fds.
	for(size_t i =0; i<3;++i ){
		assert((dup2(oldPipes[i],i)) != -1);
	}
	ptraceChildren();
	
	wait(nullptr);
	free(filename); // this could be done after execve already...
	free(childStack);
	return 0;
}
