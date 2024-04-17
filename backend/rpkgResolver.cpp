#include "rpkgResolver.hpp"
#include "../processSpawnHelper.hpp"
#include "../stringHelpers.hpp"
#include "../cFileOptHelpers.hpp"
#include <fcntl.h>
#include <cassert>

namespace {
	/*
	run 
	file <-readRDS("/home/adamep/R/x86_64-pc-linux-gnu-library/4.3/sass/Meta/package.rds")
	file$DESCRIPTION["Version"]
		Version
		"0.4.8"
	file$DESCRIPTION["Package"]
		Package
		"sass"
	file$Built$R
	[1] ‘4.3.3’
	*/

	std::optional<backend::RpkgPackage> resolvePackageFromDescription(const std::filesystem::path& filePath) {
		ArgvWrapper argv{ "-e", 
			" file <- readRDS(\"" + filePath.native() + "\");"
			"if(is.null(file$Built)){ q(\"no\",-1); };"
			"file$DESCRIPTION[\"Version\"];"
			"file$DESCRIPTION[\"Package\"];"
			"file$Built$R;"
			"q(\"no\",0)"
		};

		auto rpkg = spawnStdoutReadProcess(backend::Rpkg::executablePath, argv, []() noexcept {setlocale(LC_ALL, "C.UTF-8"); });

		backend::RpkgPackage result;

		enum {
			entry,
			versionHeader,
			version,
			packageHeader,
			package,
			rVersion,
			done,
			error,
		} state = versionHeader;

		for(auto buffer : LineIterator{ rpkg.out.get() }) {

			std::u8string_view data{ (const char8_t*)buffer.data(),buffer.size() };


			switch (state)
			{
			case entry://unused if using Rscript
				if (data == u8"\n") {
					state = versionHeader;
				}
				else {
					state = error;
				}
				break;
			case versionHeader:
				if (trim(data, u8" \n\t") == u8"Version") {
					state = version;
				}
				else {
					state = error;
				}
				break;
			case version:
				result.packageVersion = trim(data, u8" \n\t\""); //".*"\n
				if (data.empty()) {
					state = error;
				}
				else {
					state = packageHeader;
				}
				break;
			case packageHeader:
				if (trim(data, u8" \n\t") == u8"Package") {
					state = package;
				}
				else {
					state = error;
				}
				break;
			case package:
				result.packageName = trim(data, u8" \n\t\""); //".*"\n
				if (data.empty()) {
					state = error;
				}
				else {
					state = rVersion;
				}
				break;
			case rVersion:
				data.remove_prefix(3);
				data = trim(data, u8"\xE2\x80\x98\xe2\x80\x99 \n\t\"\'\'"); //the magic characters are ‘ and ’ cannot be inline unless we force the compiler to read them as utf-8
				result.rVersion = data;
				if (data.empty()) {
					state = error;
				}
				else {
					state = done;
				}
				break;
			case done:
				state = data == u8"\n" ? done : error; //allow trailing newline;
				break;
			default:
				state = error;
				break;
			}
		}

		auto waitResult = rpkg.close();

		if (waitResult != 0) {
			return std::nullopt;
		}

		if (state == done) {
			return result;
		}
		else {
			return std::nullopt;
		}
	}
	//Rscript --help. I could just check that the file exists somewhere in path but that would involve path variable resolution.
	std::optional<int> checkExist() {
		auto rpkg = spawnStdoutReadProcess(backend::Rpkg::executablePath, ArgvWrapper{"--help"});
		return rpkg.close();
	}

}

backend::Rpkg::Rpkg():exectuablePresent(checkExist() == 0){}

std::optional<backend::RpkgPackage> backend::Rpkg::resolvePathToPackage(const std::filesystem::path& fullpath)
{
	std::optional<RpkgPackage> found = std::nullopt;

	auto parent = fullpath.parent_path();
	for (std::filesystem::path lastPath = fullpath; parent != lastPath; lastPath = parent, parent = parent.parent_path()) {
		if (resolvedPaths.find(parent) != resolvedPaths.end()) {
			found = resolvedPaths.at(parent);
			break;
		}
		auto potentialPackageId = (parent / "Meta" / "package.rds");
		//this mirrors what R does https://github.com/wch/r-source/blob/trunk/src/library/base/R/library.R
		//  pfile <- system.file("Meta", "package.rds", package = package,
		//                       lib.loc = which.lib.loc)
		//if(!nzchar(pfile)) error //cannot be empty
		// then read the description. Which internally checks that the Build value is set.
		if (std::filesystem::exists(potentialPackageId)) {
			ToBeClosedFd fd{ open(potentialPackageId.c_str(),O_RDONLY | O_NONBLOCK) }; //C++ does not allow opening files ore reading in a good unblocking mode... THis also does not guarantee non blocking bt if it were say a socket bound to the filesystem...
			char buffer[1];
			if (fd && read(fd.get(), &buffer, 1) == 1) {
				//thie file exists! yay. Now we invoke R to read the package information itself.
				//we hope that the first occurence will be the one we want.
				found = resolvePackageFromDescription(potentialPackageId);
			}
			//it is possible tor a package to be loaded without being installed. Sucha a package, however, is not suitable for automated downloading and running.
			//as such, 
			if (found != std::nullopt) {
				packageNameToData.insert({ found.value().packageName, found.value() });
				break;
			}
	
		}
	}
	//we assume all things in subdirectories of a package are a package in and of itself.
	//if nothing belongs to a package, the whole path can be marked as such.
	//this cannot be done for dpkg as for example colacles folder is created by multiple packages but the files inside are clearly separated.
	resolvedPaths.try_emplace(parent,found);
	for (decltype(auto) part : std::filesystem::relative(fullpath, parent)) {
		if (part != "") {//todo: am I needed?
			resolvedPaths.try_emplace(parent /= part, found);
		}
	}
	return found;
}

bool backend::Rpkg::isKnownRpkg(const std::filesystem::path& fullpath)
{
	auto parent = fullpath.parent_path();
	for (std::filesystem::path lastPath = fullpath; parent != lastPath; lastPath = parent, parent = parent.parent_path()) {
		if (auto found = resolvedPaths.find(parent); found != resolvedPaths.end()) {
			return found->second.has_value();
		}
	}
	return false;
}
