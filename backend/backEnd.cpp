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


	bool isSpecialCased(middleend::MiddleEndState::file_info* info) {
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
			//l("etc", dir),
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
	
	
	void writeScriptLaunch(std::ostream& str, const middleend::MiddleEndState& state) {
		str << "#!/bin/sh" << std::endl;
		str << "cd '" << state.initialDir << "'" <<std::endl; //todo:escaping
		//todo switch to user

		for (auto& env : state.env) {
			str << "export '" << env << "'" << std::endl;
		}
		bool first = true;
		for (auto& arg : state.args) {
			if (first) {
				first = false;
				str << arg << " ";
			}
			else {
				str << "'" << arg << "' ";
			}
		}
		str << std::endl;
	}

	std::filesystem::path createLaunchScript(const std::filesystem::path& where, const middleend::MiddleEndState& state) {
		std::filesystem::path resPath = where / "launch.sh";
		std::ofstream file{ resPath, std::ios::openmode::_S_trunc | std::ios::openmode::_S_out }; //todo: file collision?
		writeScriptLaunch(file, state);//todo: error?
		return resPath;
	}

}
namespace backend {

	void chrootBased(const middleend::MiddleEndState& state)
	{
		char dirName[]{ "chrootTestXXXXXX\0" }; //NOT STATIC very intentionally, gets modified. Null byte because of paranoia if this ever gets changed it will need to be thought of.
		auto res =  mkdtemp(dirName);
		if (res == nullptr) {
			fprintf(stderr, "Unable to create temp directory \n");
			return;
		}
		ToBeClosedFd dir{ open(dirName, O_DIRECTORY) };
		//auto err = errno;
		if (!dir) {
			fprintf(stderr, "Unable to open temp directory for the choroot env. \n");
			return;
			//return err;
		}

		for (const auto& file : state.encounteredFilenames) {
			const auto& fileInfo = *file.second;
			transferFile(dir.get(), fileInfo.realpath);
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
	void csvBased(const middleend::MiddleEndState& state, absFilePath output)
	{
		using std::string_literals::operator""s;

		auto rpkgResolver = backend::Rpkg{}; //TODO: only use me if the R executable or its variants are detected.
		auto dpkgResolver = backend::Dpkg{};

		std::vector<middleend::MiddleEndState::file_info*> unmatchedFiles;
		std::vector<middleend::MiddleEndState::file_info*> executed;

		auto openedFile = std::ofstream{ output,std::ios::openmode::_S_trunc | std::ios::openmode::_S_out }; //c++ 23noreplace 
		openedFile << "filename" << "," << "flags" << "," << "created" << "," << "deleted" << ", needsSubfolders" << ", source dpkg package" << ", source R package" << std::endl;
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
			openedFile << "\"" << replaceAll(std::move(info->realpath.string()), "\""s, "\"\"\""s)
				<< "\"" << "," << flags_to_str(flags)
				<< "," << (info->wasInitiallyOnTheDisk.value_or(false) ? "F" : "T")
				<< "," << (info->isCurrentlyOnTheDisk.value_or(false) ? "F" : "T")
				<< "," << (info->requiresAllSubEntities.value_or(false) ? "T" : "F");

			auto dpkg = dpkgResolver.resolvePathToPackage(info->realpath);
			{

				auto resolved = replaceAll(dpkg ? std::u8string(**dpkg) : u8""s, u8"\""s, u8"\"\"\""s);
				std::string compat{ reinterpret_cast<const char*>(resolved.data()),resolved.length() };
				openedFile << ", \"" << compat << "\"";
			}

			auto rpkg = rpkgResolver.resolvePathToPackage(info->realpath);//always resolve all dependencies, the time overlap is marginal and I need to know the version even in dpkg packages.
			{
				auto resolved = replaceAll(rpkg ? std::u8string(**rpkg) : u8""s, u8"\""s, u8"\"\"\""s);
				std::string compat{ reinterpret_cast<const char*>(resolved.data()),resolved.length() };
				openedFile << ", \"" << compat << "\"";
			}
			if (!rpkg && !dpkg) {
				unmatchedFiles.emplace_back(info.get());
			}

			if (flags & fileAccessFlags::execute) {
				executed.emplace_back(info.get());
			}
			openedFile << std::endl;
		}

		auto dockerImage = std::ofstream{ output.parent_path() / "dockerImage" ,std::ios::openmode::_S_trunc | std::ios::openmode::_S_out }; //c++ 23noreplace 
		auto dockerBuildScript = std::ofstream{ output.parent_path() / "buildDocker.sh" ,std::ios::openmode::_S_trunc | std::ios::openmode::_S_out }; //c++ 23noreplace 
		//we assume R is included.
		dockerImage << "FROM ubuntu:22.04" << std::endl;
		dockerImage << "RUN apt-get update && apt-get install -y wget;" << std::endl; //ensure packages can be loaded, no upgrade?
		//this is a nasty pecial-case hack! needs to be resolved for all https things
		dockerImage << "RUN wget -qO- https://cloud.r-project.org/bin/linux/ubuntu/marutter_pubkey.asc | tee -a /etc/apt/trusted.gpg.d/cran_ubuntu_key.asc";
		for (auto& repo : dpkgResolver.aptResolver.encounteredRepositories()) {
			dockerImage << "\\\n &&  grep '" << toNormal(repo.source) << "' /etc/apt/sources.list /etc/apt/sources.list.d/*"
				" || echo '" << toNormal(repo.source) << "' >> /etc/apt/sources.list"; //try to avoid duplicates. Maybe just ef it and use apt-add-repository? - this drastically increases the image size though
			//TODO: keys https://stackoverflow.com/questions/68992799/warning-apt-key-is-deprecated-manage-keyring-files-in-trusted-gpg-d-instead/71384057#71384057
		}
		dockerImage << "\\\n && apt-get update;" << std::endl; //ensure packages can be loaded, no upgrade?
		//temp solution: --allow-unauthenticated
		//todo: escaping
		if (!dpkgResolver.packageNameToData.empty()) {
			dockerImage << "RUN export DEBIAN_FRONTEND=noninteractive && apt-get install -y ";
			for (auto& package : dpkgResolver.packageNameToData) {
				dockerImage << std::string(package) << " "; //TODO; why are versions sometimes not resolving even though we have added everything?
			}
			dockerImage << std::endl;
		}

		//this is done before the R packages to ensure all  library paths are accessible

		//TODO:
		///usr/lib/R/etc/Renviron.site coudl get modified. So coudl probably any other file in dpkg packages...
		//this can be done before the program is ever run
		//todo: what if it is deleted now?
		std::unordered_map<absFilePath, std::string> dockerPathTranslator;

		{
			//TODO: handle symlinks
			std::unordered_set<std::filesystem::path> folders = rpkgResolver.getLibraryPaths();
			for (auto file : unmatchedFiles) {
				if (file->wasInitiallyOnTheDisk.value_or(false) && file->isCurrentlyOnTheDisk.value_or(true)) {
					if (!isSpecialCased(file)) {
						if (file->type == std::remove_reference_t<decltype(file->type.value())>::dir
							|| (!file->type.has_value() && std::filesystem::is_directory(file->realpath))) {
							folders.emplace(file->realpath);
							//TODO: handle symlinks
						}
						else if (file->type == std::remove_reference_t<decltype(file->type.value())>::file 
							|| (!file->type.has_value() && std::filesystem::is_regular_file(file->realpath))) {
							dockerPathTranslator.emplace(file->realpath, std::to_string(dockerPathTranslator.size()));
							folders.emplace(file->realpath.parent_path());
						}
						else {
							folders.emplace(file->realpath.parent_path());
						}
					}
				}
			}

			if (!folders.empty()) {
				dockerImage << "RUN mkdir -p ";
				for (auto& folder : folders) {
					dockerImage << folder << " ";//autoquoted
				}
				dockerImage << std::endl;
			}
		}
		

		auto RpkgDirect = rpkgResolver.packageNameToData.size();
		//todo: check R version
		if (!rpkgResolver.packageNameToData.empty()) {
			//this will break if ran directly due to too large a string argument. I do not know the specifics but passing it into a file and executing the file works.


			dockerImage << "RUN echo '"
				"cores = min(parallel::detectCores(),4);"//parallel is a part of the core on ubuntu...
				"tmpDir <- tempdir();"
				"install.packages(\"remotes\",lib=c(tmpDir));"
				"require(\"remotes\",lib.loc = c(tmpDir)); "; //this way the require should not conflict with the installed version.
			//TODO: if we wanted to be fancy, instead get a vector of all the R packages currently topsorted. Install these in parallel.
			for (auto& package : rpkgResolver.topSortedPackages()) {
				if (package.isBaseRpackage) {
					//TODO: add a check into the launch.sh perhaps?
					continue;//these canot be installed
				}
				//todo: how about default-bundled packages? do we need to check their version as well?
				//see for example /usr/lib/R/library/stats/Meta/nsInfo.rds
				// this works due to a combination of things explained here https://stat.ethz.ch/pipermail/r-devel/2018-October/076989.html
				// and here https://stackoverflow.com/questions/17082341/installing-older-version-of-r-package
				dockerImage << "install_version(\"" << toNormal(package.packageName) << "\",\"" << toNormal(package.packageVersion) << "\"" <<
					",upgrade = \"never\", dependencies=F,lib=c("<<package.whereLocated.parent_path()<<"), Ncpus=cores" << "); ";//topsorted
				//unfortunatelly upgrade=never is not sufficient
				// the package may depend on other packages which are not installed and such packages would then get installed at possibly higher versions than intended
				//TODO: add parsing of the source param to get the potential github and such install rather than this api.
				//TODO: add check that the version is detected here first

				//TODO: this may involve `interesting` applications of checking random paths on the system to ensure that the package is found.
				// but most likely will just end up

			}
			dockerImage << "' > R_Install && Rscript R_Install" << std::endl;
		}



		//COPY src dest does not work as it quickly exhaust the maximum layer depth
		//instead a folder with all the relevant data is serialised to and then added to the image where it is unserialised.


		auto file = createLaunchScript(".",state);
		auto absPath = std::filesystem::absolute(file).lexically_normal();
		dockerPathTranslator.emplace(absPath, std::to_string(dockerPathTranslator.size()));
		if (!dockerPathTranslator.empty()) {
			dockerImage << "ADD DockerData /tmp/DockerData" << std::endl;

			dockerImage << "RUN " ;//unpack
			for (auto& [path, key] : dockerPathTranslator) {
				dockerImage << "mv /tmp/DockerData/" << key << " " << path << " && ";
			}
			dockerImage << "true" << std::endl;
		}


		for (auto& env : state.env) {
			dockerImage << "ENV " << replaceFirst(std::string(env), "="s, "=\""s) << "\"" << std::endl;
		}
		dockerImage << "WORKDIR " << state.initialDir << std::endl;
		//TODO: state.users;

		dockerBuildScript << "#!/bin/sh" << std::endl << "mkdir 'DockerData'; cd 'DockerData'; ";
		for (const auto& [source, dest] : dockerPathTranslator) {
			dockerBuildScript << "cp " << source << " " << dest << std::endl; //todo: escaping, ln if the file is on the same filesystem. Or amybe create a condition if ln fails, fall back to cp.
		}
		dockerBuildScript << "cd ..; echo '*' > '.dockerignore'; echo '!DockerData' >> '.dockerignore';";
		dockerBuildScript << "docker build -t 'diplomka:test' -f dockerImage .; " << std::endl;
		dockerBuildScript << "rm -rf 'DockerData'; rm '.dockerignore';" << std::endl;
		dockerBuildScript << "docker run  -it --entrypoint bash  diplomka:test" << std::endl;

		auto report = std::ofstream{ output.parent_path() / "report.txt",std::ios::openmode::_S_trunc | std::ios::openmode::_S_out };

		report << "Discovered " << state.encounteredFilenames.size() << " different file dependencies" << std::endl;
		report << "Of those " << unmatchedFiles.size() << " were not installed using any form of package manager" << std::endl;
		report << "Dpkg provides " << dpkgResolver.packageNameToData.size() << " different packages the program depends on. " << " This does not include dependencies of the packages which were not directly used during the program runtime " << std::endl;
		report << "R provides " << RpkgDirect << " packages used directly and " << rpkgResolver.packageNameToData.size() << " packages the program directly or indirectly depends on." << std::endl;
		
		
		report << "The program incuding itself executed " << executed.size() << " files. Namely:" << std::endl;
		for (decltype(auto) it : executed) {
			report << it->realpath << std::endl;
		}
	}

}