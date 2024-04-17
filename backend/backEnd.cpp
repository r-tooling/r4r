#include "backEnd.hpp"
#include "dpkgResolver.hpp"
#include "rpkgResolver.hpp"
#include "../toBeClosedFd.hpp"
#include "../stringHelpers.hpp"
#include "filesystemGraph.hpp"
#include <cstdlib>
#include <fcntl.h> //O_* flags
#include <sys/stat.h> //mkdirat
#include <cassert>
#include <unistd.h>
#include <fstream>
#include <unordered_map>
#include <variant>

namespace {

	enum fileAccessFlags {
		read = 1,
		write = 2,
		execute = 4
	};
	//src: https://stackoverflow.com/questions/2896600/how-to-replace-all-occurrences-of-a-character-in-string
	template<class T>
	T&& replaceAll(T&& source, const T&& from, const T&& to)
	{
		using noref = std::remove_reference_t<T>;
		noref newString{};
		newString.reserve(source.length());  // avoids a few memory allocations

		typename noref::size_type lastPos = 0;
		typename noref::size_type findPos;

		while (noref::npos != (findPos = source.find(from, lastPos)))
		{
			newString.append(source, lastPos, findPos - lastPos);
			newString += to;
			lastPos = findPos + from.length();
		}

		// Care for the rest after last occurrence
		newString += source.substr(lastPos);

		source.swap(newString);
		return std::forward<T>(source);
	}

	template<class T>
	T&& replaceFirst(T&& source, const T&& from, const T&& to)
	{
		using noref = std::remove_reference_t<T>;
		noref newString{};
		newString.reserve(source.length());  // avoids a few memory allocations

		typename noref::size_type lastPos = 0;
		typename noref::size_type findPos;

		if(noref::npos != (findPos = source.find(from, lastPos)))
		{
			newString.append(source, lastPos, findPos - lastPos);
			newString += to;
			lastPos = findPos + from.length();
		}

		// Care for the rest after last occurrence
		newString += source.substr(lastPos);

		source.swap(newString);
		return std::forward<T>(source);
	}

	std::string flags_to_str(int flags) {
		std::string res = "";
		if (flags & fileAccessFlags::read) {
			res += "R";
		}
		if (flags & fileAccessFlags::write) {
			res += "W";
		}
		if (flags & fileAccessFlags::execute) {
			res += "X";
		}
		return res;
	}

	void transferFile(fileDescriptor chrootDirectory, absFilePath oldPath) {
		//also see openat2 RESOLVE_IN_ROOT flag for relative paths

		//hardlink based-solution
		//open old file
		ToBeClosedFd oldFile { open(oldPath.c_str(), O_RDONLY) };
		ToBeClosedFd currentDir { dup(chrootDirectory) };
		for (auto begin = oldPath.begin(),
			next = ++begin, //the last file shall not be considered here.
			end = oldPath.end();
			begin != end && next != end;
			++begin, ++next
			) {
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
			ToBeClosedFd potentialDir{ openat((fileDescriptor)currentDir, relPath.c_str(), O_DIRECTORY) };
			if (!potentialDir) {
				potentialDir.reset(mkdirat((fileDescriptor)currentDir, relPath.c_str(), 0777));
				if (!potentialDir) {
					printf("cannot make directory %s from %s\n", relPath.c_str(), oldPath.c_str());
					return;
				}
			}
			currentDir.swap(potentialDir);
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
		/*assert(linkat(oldFile, "",
			currentDir, oldPath.filename().c_str(), AT_EMPTY_PATH) == 0);*/
		printf("transferred %s\n", oldPath.c_str());

	}


	bool isSpecialCased(MiddleEndState::file_info* info) {
		auto none = Graph::operationType::none;
		auto dir = Graph::operationType::directory;
		auto exa = Graph::operationType::exact;
		//node constructor
		auto n = [none] <ptr ...T> (std::string s, T... i) {
			static_assert(sizeof...(i) > 0);
			return std::make_unique<Graph>(s, none, std::move(i)...);
		};
		//leaf constructor
		auto l = [] (std::string s, Graph::operationType d) {
			return std::make_unique<Graph>(s, d);
		};

		static const auto root = 
		n("/", 
			l("proc", dir),
			l("etc", dir),
			l("sys", dir),
			l("dev", dir),
			n("usr", n("lib", n("locale", l("locale-archive", exa))))
			);///usr/lib/locale/locale-archive



		const auto& path = info->realpath;
		int counter = 0;
		const Graph* current = root.get();
		auto iter = path.begin(), end = path.end();
		if (iter != end && *iter == "/") {//do not try to match the root directory. Consider erroring out otherwise
			++iter;
		}
		for (; iter != end; iter++,counter++) {
			if (auto item = current->files.find(iter->string()); item != current->files.end()) {
				current = item->get();
			}
			else {
				auto& doOpt = current->operation;
				if (doOpt == dir) {
					return true;
				}
				else if (doOpt == exa) {
					return false;
				}
				else if (doOpt == none) {
					return false;
				}
			}
		}
		auto& doOpt = current->operation;
		if (doOpt == dir) {
			return true;
		}
		else if (doOpt == exa) {
			return iter == end;
		}
		else if (doOpt == none) {
			return false;
		}
		return false;
	}
	
	

}


void chrootBased(const MiddleEndState& state)
{
	char dirName[]{ "chrootTestXXXXXX\0" }; //NOT STATIC very intentionally, gets modified. Null byte because of paranoia if this ever gets changed it will need to be thought of.
	assert(mkdtemp(dirName) != nullptr);
	ToBeClosedFd dir{ open(dirName, O_DIRECTORY) };
	//auto err = errno;
	if (!dir) {
		fprintf(stderr, "Unable to open temp directory for the choroot env. \n");
		return;
			//return err;
	}

	for (const auto& file : state.encounteredFilenames) {
		const auto& fileInfo = *file.second;
		transferFile(dir.get(),fileInfo.realpath);
		for (const auto& rel : fileInfo.accessibleAs) {
			if (rel.relPath.is_absolute()) {
				transferFile(dir.get(), rel.relPath);//TODO: duplicates
			}
			else {
				//TODO: we pray this works now.
			}
		}
		
		//create a link for the file. Ensure access rights are OK.
	}

	printf("test from %s", dirName);
}

/*
* FORMAT
* FILENAME, FLAGS
*/
void csvBased(const MiddleEndState& state, absFilePath output)
{
	using std::string_literals::operator""s;

	auto rpkgResolver = backend::Rpkg{}; //TODO: only use me if the R executable or its variants are detected.
	auto dpkgResolver = backend::Dpkg{};

	std::vector<MiddleEndState::file_info*> unmatchedFiles;
	std::unordered_set<std::u8string> repositories;


	auto openedFile = std::ofstream{ output,std::ios::openmode::_S_trunc | std::ios::openmode::_S_out }; //c++ 23noreplace 
	openedFile << "filename" << "," << "flags" << "," << "created" << "," << "deleted" << ", source dpkg package" << ", source R package" << std::endl;
	for (auto& [path, info] : state.encounteredFilenames) {//TODO: trace absolute paths as well as relative paths.
		auto flags = 0;
		for (auto& relative : info->accessibleAs) {
			if (relative.executable) {
				flags |= fileAccessFlags::execute;
			}
			if (relative.flags) {
				if ((*relative.flags & O_ACCMODE) == O_RDONLY) {
					flags |= fileAccessFlags::read;
				}
				else if ((*relative.flags & O_ACCMODE) == O_WRONLY) {
					flags |= fileAccessFlags::write;
				}
				else if ((*relative.flags & O_ACCMODE) == O_RDWR) {
					flags |= fileAccessFlags::read | fileAccessFlags::write;
				}
			}

		}
		openedFile << "\"" << replaceAll(std::move(info->realpath.string()), "\""s, "\"\"\""s) << "\"" << "," << flags_to_str(flags)
			<< "," << (info->wasInitiallyOnTheDisk.value_or(false) ? "F" : "T")
			<< "," << (info->isCurrentlyOnTheDisk.value_or(false) ? "F" : "T");
		
		

		auto dpkg = rpkgResolver.isKnownRpkg(info->realpath) ? std::nullopt : dpkgResolver.resolvePathToPackage(info->realpath);
		{
			if (dpkg) {
				repositories.emplace(dpkg.value().packageRepo);
			}

			auto resolved = replaceAll(dpkg ? (std::u8string)dpkg.value() : u8""s, u8"\""s, u8"\"\"\""s);
			std::string compat{ reinterpret_cast<const char*>(resolved.data()),resolved.length()};
			openedFile << ", \"" << compat << "\"";
		}
		auto rpkg = !dpkg ? rpkgResolver.resolvePathToPackage(info->realpath) : std::nullopt;
		{
			auto resolved = replaceAll(rpkg ? (std::u8string)rpkg.value() : u8""s, u8"\""s, u8"\"\"\""s);
			std::string compat{ reinterpret_cast<const char*>(resolved.data()),resolved.length() };
			openedFile << ", \"" << compat << "\"";
		}
		openedFile << std::endl;
		if (!rpkg && !dpkg) {
			unmatchedFiles.emplace_back(info.get());
		}

	}

	auto dockerImage = std::ofstream{ output.parent_path() /"dockerImage" ,std::ios::openmode::_S_trunc | std::ios::openmode::_S_out}; //c++ 23noreplace 
	//we assume R is included.
	dockerImage << "FROM ubuntu:22.04" << std::endl;
	dockerImage << "RUN apt-get update;" << std::endl; //ensure packages can be loaded, no upgrade?
	dockerImage << "RUN apt-get install -y wget;" << std::endl;
	//this is a nasty pecial-case hack! needs to be resolved for all https things
	dockerImage << "RUN wget -qO- https://cloud.r-project.org/bin/linux/ubuntu/marutter_pubkey.asc | tee -a /etc/apt/trusted.gpg.d/cran_ubuntu_key.asc";
	for (auto& repo : repositories) {
		if (!repo.empty()) {
			dockerImage << "\\\n &&  grep '" << toNormal(repo) << "' /etc/apt/sources.list /etc/apt/sources.list.d/*"
				" || echo '" << toNormal(repo) << "' >> /etc/apt/sources.list"; //try to avoid duplicates. Maybe just ef it and use apt-add-repository?
		}
		//TODO: keys https://stackoverflow.com/questions/68992799/warning-apt-key-is-deprecated-manage-keyring-files-in-trusted-gpg-d-instead/71384057#71384057
	}
	dockerImage << std::endl << "RUN apt-get update;" << std::endl; //ensure packages can be loaded, no upgrade?
	//temp solution: --allow-unauthenticated
	//todo: escaping
	if (!dpkgResolver.packageNameToData.empty()) {
		dockerImage << "RUN export DEBIAN_FRONTEND=noninteractive && apt-get install -y ";
		for (auto& [name, package] : dpkgResolver.packageNameToData) {
			dockerImage << std::string(package) << " "; //TODO; why are versions sometimes not resolving even though we have added everything?
		}
		dockerImage << std::endl;
	}
	//todo: check R version
	if (!rpkgResolver.packageNameToData.empty()) {
		//TODO: what if the actual location of the package matters as well eg. a directory is hardcoded.
		dockerImage << "RUN Rscript -e 'install.packages(\"remotes\"); require(remotes); ";
		for (auto& [name, package] : rpkgResolver.packageNameToData) {
			//todo: how about default-bundled packages? do we need to check their version as well?
			//see for example /usr/lib/R/library/stats/Meta/nsInfo.rds
			// this works due to a combination of things explained here https://stat.ethz.ch/pipermail/r-devel/2018-October/076989.html
			// and here https://stackoverflow.com/questions/17082341/installing-older-version-of-r-package
			dockerImage << "install_version(\""<< toNormal(package.packageName) << "\",\"" << toNormal(package.packageVersion) << "\");";
		}
		dockerImage << "'" << std::endl;
	}
	//todo: what if it is deleted now?
	for (auto file : unmatchedFiles) {
		if (file->wasInitiallyOnTheDisk.value_or(false)) {
			if (!isSpecialCased(file)) {
				if (file->type == std::remove_reference_t<decltype(file->type.value())>::dir) {
					//TODO: remove duplicates, handle symlinks
					dockerImage << "RUN mkdir -p " << file->realpath << "" << std::endl;//autoquoted
				}
				else {
					dockerImage << "RUN mkdir -p " << file->realpath.parent_path() << "" << std::endl;
				}
			}
		}
	}
	for (auto file : unmatchedFiles) {
		if (file->wasInitiallyOnTheDisk.value_or(false)) {
			if (!isSpecialCased(file)) {
				if (file->type != std::remove_reference_t<decltype(file->type.value())>::dir) {
					//todo: accessible as handling.
					dockerImage << "COPY " << file->realpath << " " << file->realpath << std::endl;
				}
			}

		}
	}


	for (auto& env : state.env) {
		dockerImage << "ENV " << replaceFirst(std::string(env),"="s,"=\""s) << "\"" << std::endl;
	}
	dockerImage << "WORKDIR " << state.initialDir << std::endl;
	//TODO: state.users;
}
