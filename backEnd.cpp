#include "backEnd.hpp"

#include <cstdlib>
#include <fcntl.h> //O_* flags
#include <sys/stat.h> //mkdirat
#include <cassert>
#include <unistd.h>

void transferFile(fileDescriptor chrootDirectory, absFilePath oldPath) {
	//also see openat2 RESOLVE_IN_ROOT flag for relative paths

	//hardlink based-solution
	//open old file
	auto oldFile = open(oldPath.c_str(),O_RDONLY);
	auto currentDir = dup(chrootDirectory);//todo: avoid the need for dup here
	for (auto begin = oldPath.begin(),
		next = ++begin, //the last file shall not be considered here.
		end = oldPath.end();
		begin != end && next != end;
		++begin,++next
		){
		const auto& relPath = *next;
		if (relPath.is_absolute()) { //ignore the absolute prefix
			continue;
		}
		//open has a bug, though:
		/*
		When both O_CREAT and O_DIRECTORY are specified in flags and the
       file specified by pathname does not exist, open() will create a
       regular file (i.e., O_DIRECTORY is ignored).
		*/
		auto potentialDir = openat(currentDir, relPath.c_str(), O_DIRECTORY);
		if (potentialDir < 0) {
			potentialDir = mkdirat(currentDir, relPath.c_str(), 0777);
			if (potentialDir < 0) {
				printf("cannot make directory %s from %s\n", relPath.c_str(),oldPath.c_str());
				close(currentDir);
				return;
			}
		}
		close(currentDir);
		currentDir = potentialDir;
	}
	/*
	
	 AT_EMPTY_PATH (since Linux 2.6.39)
              If oldpath is an empty string, create a link to the file
              referenced by olddirfd (which may have been obtained using
              the open(2) O_PATH flag).  In this case, olddirfd can
              refer to any type of file except a directory.  This will
              generally not work if the file has a link count of zero
              (files created with O_TMPFILE and without O_EXCL are an
              exception).  The caller must have the CAP_DAC_READ_SEARCH
              capability in order to use this flag.  This flag is Linux-
              specific; define _GNU_SOURCE to obtain its definition.
	*/
	//todo: ahndle folders
	assert(linkat(oldFile, "",
		currentDir, oldPath.filename().c_str(), AT_EMPTY_PATH) == 0);
	close(oldFile);
	printf("transferred %s\n", oldPath.c_str());

}

void chrootBased(const MiddleEndState& state)
{
	char dirName[]{ "chrootTestXXXXXX\0" }; //NOT STATIC very intentionally, gets modified. Null byte because of paranoia if this ever gets changed it will need to be thought of.
	assert(mkdtemp(dirName) != nullptr);
	auto dir = open(dirName, O_DIRECTORY);
	assert(dir >= 0);
	for (const auto& file : state.encounteredFilenames) {
		const auto& fileInfo = *file.second;
		transferFile(dir,fileInfo.realpath);
		for (const auto& rel : fileInfo.accessibleAs) {
			if (rel.relPath.is_absolute()) {
				transferFile(dir, rel.relPath);//TODO: duplicates
			}
			else {
				//TODO: we pray this works now.
			}
		}
		
		//create a link for the file. Ensure access rights are OK.
	}

	printf("test from %s", dirName);
}


void report(const MiddleEndState& state)
{
	for (auto& file : state.encounteredFilenames) {//TODO: trace absolute paths as well as relative paths.
		printf("%s - ", file.second->realpath.c_str());
		for (auto& relative : file.second->accessibleAs) {
			if (relative.relPath != file.second->realpath) {
				printf("%s -", relative.relPath.c_str());
			}
			if (relative.relPath.is_relative()) {
				printf("from path: %s ", relative.workdir.c_str());
			}

			if ((relative.flags & O_ACCMODE) == O_RDONLY) {
				printf("READONLY");
			}
			else if ((relative.flags & O_ACCMODE) == O_WRONLY) {
				printf("WRITEONLY");
			}
			else if ((relative.flags & O_ACCMODE) == O_RDWR) {
				printf("R/W");
			}
			else {
				assert(false);//no reasonable system should reach here;
			}
			if (relative.executable) {
				printf("+X");
			}
		}
		printf("\n");
	}
}
