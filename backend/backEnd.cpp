#include "backEnd.hpp"
#include "dpkgResolver.hpp"
#include "rpkgResolver.hpp"
#include "../toBeClosedFd.hpp"
#include "../stringHelpers.hpp"
#include "filesystemGraph.hpp"
#include "optionals.hpp"
#include <cstdlib>
#include <fcntl.h> //O_* flags
#include <sys/stat.h> //mkdirat
#include <cassert>
#include <unistd.h>
#include <fstream>
#include <unordered_map>
#include <variant>


namespace {

	struct SubdirIterator {
		std::filesystem::path pathToWalk;

		struct dummy {

		};

		struct IterImpl {
			const std::filesystem::path& origPath;
			std::filesystem::path::iterator currentPos;
			std::filesystem::path soFar;

			IterImpl(const std::filesystem::path& origPath) :origPath{ origPath }, currentPos(origPath.begin()), soFar{ currentPos != origPath.end() ? *currentPos : "" } {};

			const auto& operator*() {
				return soFar;
			}

			auto operator++() {
				++currentPos;
				if (currentPos != origPath.end())
					soFar /= *currentPos;
				return this;
			}

			auto operator!=(const dummy&) {
				return currentPos != origPath.end();
			}
		};

		auto begin() {
			return IterImpl(pathToWalk);
		}
		auto end() {
			return dummy{};
		}

	};

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


	bool isSpecialCased(const std::filesystem::path& path) {
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
			n("etc", l("ld.so.cache",exa)),
			l("sys", dir),
			l("dev", dir),
			n("usr", n("lib", n("locale", l("locale-archive", exa))))
			);///usr/lib/locale/locale-archive

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
	std::string escapeForCSV(std::u8string unescaped) {
		using std::string_literals::operator""s;
	//	if (unescaped.contains(strType{ "," })) { c++23
		if (unescaped.find(u8",") != unescaped.npos) {
			auto s = u8"\""s + replaceAll(std::move(unescaped), u8"\""s, u8"\"\"\""s) + u8"\""s ;
			return std::string{ reinterpret_cast<const char*>(s.data()),s.length() };
		}
		else {
			return std::string{ reinterpret_cast<const char*>(unescaped.data()),unescaped.length() };
		}
	}

	template<class Key, class Result>
	std::optional<Result> optionalResolve(const std::unordered_map<Key,Result>& container, const Key& key) {
		if (auto ptr = container.find(key); ptr != container.end())
			return ptr->second;
		else
			return std::nullopt;
	}

	void appendSymlinkResult(std::unordered_set<absFilePath>& resultStore, std::filesystem::path symlink, std::unordered_set<absFilePath> ignoreEqual) {
		auto target = std::filesystem::read_symlink(symlink);
		if (!target.is_absolute()) {
			target = symlink.parent_path() / target;
		}
		target = target.lexically_normal();
		if (!ignoreEqual.contains(target)) {
			resultStore.emplace(target);
			ignoreEqual.emplace(target);
			if (std::filesystem::is_symlink(target)) {
				return appendSymlinkResult(resultStore, target, ignoreEqual);
			}
		}
		return;
	}
	void unpackFiles(std::unordered_map<absFilePath, std::string>& dockerPathTranslator, std::ofstream& dockerImage)
	{
		if (!dockerPathTranslator.empty()) {
			dockerImage << "ADD DockerData /tmp/DockerData" << std::endl;

			dockerImage << "RUN ";//unpack
			for (auto& [path, key] : dockerPathTranslator) {
				dockerImage << "mv /tmp/DockerData/" << key << " " << path << " && ";
			}
			dockerImage << "true" << std::endl;
		}
	}

	static void createBuildScript(std::ofstream& dockerBuildScript,
		std::unordered_map<absFilePath, std::string>& dockerPathTranslator,
		const std::string_view& tag,
		std::ofstream& runDockerScript,
		std::unordered_set<std::filesystem::path> unignoredFiles)
	{
		std::ofstream dockerignore{ ".dockerignore" };
		dockerignore << "*" << std::endl;
		dockerignore << "!DockerData" << std::endl;
		for (auto& item : unignoredFiles) {
			dockerignore << "!" << item.filename().string() << std::endl;
		}

		dockerBuildScript << "#!/bin/sh" << std::endl << "mkdir 'DockerData'; cd 'DockerData'; ";
		for (const auto& [source, dest] : dockerPathTranslator) {
			dockerBuildScript << "cp " << source << " " << dest << std::endl; //todo: escaping, ln if the file is on the same filesystem. Or amybe create a condition if ln fails, fall back to cp.
		}
		
		dockerBuildScript << "cd ..;";
		dockerBuildScript << "docker build -t '" << tag << "' -f dockerImage .; success=$?;" << std::endl;
		dockerBuildScript << "rm -rf 'DockerData';" << std::endl;
		dockerBuildScript << "if [ $success -eq 0 ]; then ./runDocker.sh; fi;" << std::endl;

		runDockerScript << "docker run  -it --entrypoint bash " << tag << std::endl;
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

	void CachingResolver::csv(absFilePath output)
	{
		using std::string_literals::operator""s;
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
			auto dpkg = optTransform(optionalResolve(dpkgResolver.resolvedPaths, path).value_or(std::nullopt), [](const auto* package) {return static_cast<std::u8string>(*package); });
			openedFile << "," << escapeForCSV(dpkg.value_or(u8""s));
			auto rpkg = optTransform(optionalResolve(rpkgResolver.resolvedPaths, path).value_or(std::nullopt), [](const auto* package) {return static_cast<std::u8string>(*package);});
			openedFile << "," << escapeForCSV(rpkg.value_or(u8""s));
			openedFile << std::endl;
		}
		
	}

	void CachingResolver::report(absFilePath output)
	{
		auto report = std::ofstream{ output,std::ios::openmode::_S_trunc | std::ios::openmode::_S_out };
		auto exec = getExecutedFiles();
		report << "Discovered " << state.encounteredFilenames.size() << " different file dependencies" << std::endl;
		report << "Of those " << getUnmatchedFiles().size() << " were not installed using any form of package manager" << std::endl;
		report << "Dpkg provides " << dpkgResolver.packageNameToData.size() << " different packages the program depends on. " << " This does not include dependencies of the packages which were not directly used during the program runtime " << std::endl;
		report << "R provides " << rpkgResolver.packageNameToData.size() << " packages used directly and " << rpkgResolver.topSortedPackages().items.size() << " packages the program directly or indirectly depends on." << std::endl;
		report << "The program incuding itself executed " << exec.size() << " files. Namely:" << std::endl;

		for (decltype(auto) it : exec) {
			report << it->realpath << std::endl;
		}

	}
	void CachingResolver::dockerImage(absFilePath output,const std::string_view tag)
	{
		using std::string_literals::operator""s;



		auto dockerImage = std::ofstream{ output.parent_path() / "dockerImage" ,std::ios::openmode::_S_trunc | std::ios::openmode::_S_out }; //c++ 23noreplace 
		auto dockerBuildScript = std::ofstream{ output.parent_path() / "buildDocker.sh" ,std::ios::openmode::_S_trunc | std::ios::openmode::_S_out }; //c++ 23noreplace 
		auto runDockerScript = std::ofstream{ output.parent_path() / "runDocker.sh" ,std::ios::openmode::_S_trunc | std::ios::openmode::_S_out }; //TODO:

		dockerImage << "FROM ubuntu:22.04" << std::endl;
		

		
		dpkgResolver.persist(dockerImage);
		//this is done before the R packages to ensure all  library paths are accessible
		std::filesystem::path directoryCreator{ output.parent_path() / "recreateDirectories.sh"};
		persistDirectoriesAndSymbolicLinks(dockerImage, directoryCreator);

		std::filesystem::path rpkgCreator{ output.parent_path() / "installRPackages.sh" };
		rpkgResolver.persist(dockerImage, rpkgCreator);


		//COPY src dest does not work as it quickly exhaust the maximum layer depth
		//instead a folder with all the relevant data is serialised to and then added to the image where it is unserialised.

		// PERSIST FILES

		std::unordered_map<absFilePath, std::string> dockerPathTranslator;

		auto file = createLaunchScript(".", state);
		dockerPathTranslator.emplace(std::filesystem::canonical(file), std::to_string(dockerPathTranslator.size()));
		for (auto info : getUnmatchedFiles()) {
			if (isSpecialCased(info->realpath)) {
				continue;
			}
			if (std::filesystem::is_regular_file(info->realpath)) {
				dockerPathTranslator.emplace(info->realpath, std::to_string(dockerPathTranslator.size()));
			}
			//TODO: error on non symlink/dir/file
		}



		unpackFiles(dockerPathTranslator, dockerImage);

		//PERSIST ENV

		for (auto& env : state.env) {
			dockerImage << "ENV " << replaceFirst(std::string(env), "="s, "=\""s) << "\"" << std::endl;
		}
		dockerImage << "WORKDIR " << state.initialDir << std::endl;


		createBuildScript(dockerBuildScript, dockerPathTranslator, tag, runDockerScript, {directoryCreator, rpkgCreator });
	}

	std::vector<middleend::MiddleEndState::file_info*> CachingResolver::getUnmatchedFiles()
	{
		std::vector<middleend::MiddleEndState::file_info*> res{};
		for (auto& [path, info] : state.encounteredFilenames) {
			auto dpkg = optionalResolve(dpkgResolver.resolvedPaths, path).value_or(std::nullopt);
			auto rpkg = optionalResolve(rpkgResolver.resolvedPaths, path).value_or(std::nullopt);
			if (!dpkg && !rpkg) {
				res.emplace_back(info.get());
			}
		}
		return res;
	}

	std::vector<middleend::MiddleEndState::file_info*> CachingResolver::getExecutedFiles()
	{
		std::vector<middleend::MiddleEndState::file_info*> res{};
		for (auto& [path, info] : state.encounteredFilenames) {
			for (auto& val : info.get()->accessibleAs) {
				if (val.executable) {
					res.emplace_back(info.get());
					break;//inner loop only
				}
			}
		}
		return res;
	}
	/*
	Package repositories may "own" files under both the fullpath and a symlinked path
	/usr/lib/a ->
	/lib/a ->
	we may only ever see one of these accessed. As such, the current "dumb" way of matching files has to stay.

	but it can be done in batches.
	
	*/

	std::unordered_set<absFilePath> CachingResolver::symlinkList()
	{
		

		std::unordered_set<absFilePath> currentList;
		//Resolving symlinks
		//take all the non-realpath ways to access this item
		for (auto& [_, info] : state.encounteredFilenames) {
			for (auto& access : info->accessibleAs) {

				//if the file path is an absolute path
				auto path = std::filesystem::path{};
				if (access.relPath.is_absolute()) {
					path = access.relPath.lexically_normal();
				}
				else {
					path = (access.workdir / access.relPath).lexically_normal();
				}
				if (path != info->realpath) {
					if (std::filesystem::is_symlink(path)) {
						appendSymlinkResult(currentList, path, { info->realpath });
					}
					currentList.emplace(std::move(path));
				}
			}
		}
		return currentList;
	}
	

	void CachingResolver::persistDirectoriesAndSymbolicLinks(std::ostream& dockerImage, const std::filesystem::path& scriptLocation)
	{
		std::unordered_set<std::filesystem::path> resolvedPaths;

		std::ofstream result{ scriptLocation, std::ios::openmode::_S_trunc | std::ios::openmode::_S_out };

		//TODO: add a method for detecting that a sublink here belongs to a package and mark it as resolved.
		//but this requires resolving all the symlinks which are to be created to their real paths as we go along. 
		auto all = symlinkList();
		for (const auto& [path, _] : state.encounteredFilenames) {
			all.emplace(path);
		}
		//These should not actually be required but better safe than sorry in this case. Any found library should have been detected before.
		all.merge(rpkgResolver.getLibraryPaths()); 
		all.emplace(state.initialDir);
		for (decltype(auto) path : all) {
			//do not persist links which will be persisted by dpkg.
			bool ignoreFinal = false;
			if (auto found = dpkgResolver.resolvedPaths.find(path); found != dpkgResolver.resolvedPaths.end()) {
				ignoreFinal = found->second.has_value();
			}

			for (const auto& segment : SubdirIterator(path)) {
				if (isSpecialCased(segment) || (ignoreFinal && segment == path)) {
					continue;
				}
				if (!resolvedPaths.contains(segment)) {
					if (std::filesystem::is_directory(segment)) {
						result << "mkdir -p " << segment << std::endl; //-p is there just to be absolutely sure everything works. could be ommited
					}
					else if (std::filesystem::is_symlink(segment)) {
						result << "ln -s " << std::filesystem::read_symlink(segment) << " " << segment << std::endl;
					}
					resolvedPaths.emplace(segment);
				}
			}
		}
		if (!resolvedPaths.empty()) {
			dockerImage << "COPY [" << scriptLocation << "," << scriptLocation << "]" << std::endl;
			dockerImage << "RUN bash " << scriptLocation << " || true" << std::endl; //we always want this to complete even if the directories did not get created
		}
	}



	void CachingResolver::resolveRPackages()
	{
		if (!rpkgResolver.areDependenciesPresent()) {
			fprintf(stderr, "Unable to resolve R packages as the required dependencies are not present\n");
			return;
		}

		//TODO: only use me for files if the R executable or its variants are detected in accesses.
		for (auto& [path, info] : state.encounteredFilenames) {
			rpkgResolver.resolvePathToPackage(info->realpath);//always resolve all dependencies, the time overlap is marginal and I need to know the version even in dpkg packages.
		}

		//Resolving symlinks
		//take all the non-realpath ways to access this item
		for (auto& info : symlinkList()) {
			rpkgResolver.resolvePathToPackage(info);
		}

	}
	void CachingResolver::resolveDebianPackages()
	{
		if (!dpkgResolver.areDependenciesPresent()) {
			fprintf(stderr, "Unable to resolve DPKG/APT packages as the required dependencies are not present\n");
			return;
		}
		std::unordered_set<std::filesystem::path> what;

		for (auto& [path, info] : state.encounteredFilenames) {
			what.emplace(info->realpath);
		}
		dpkgResolver.batchResolvePathToPackage(what);
		what = symlinkList();

		dpkgResolver.batchResolvePathToPackage(std::move(what),true);

	}

}