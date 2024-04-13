#include "rpkgResolver.hpp"
#include "../processSpawnHelper.hpp"
#include "../stringHelpers.hpp"
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
	enum parsingState {
		entry,
		versionHeader,
		version,
		packageHeader,
		package,
		rVersion,
		done,
		error,
	};

	void indirectDelete(char** ptr) { if (*ptr) free(*ptr); }

	std::optional<backend::RpkgPackage> resolvePackageFromDescription(const std::filesystem::path& filePath) {
		auto localToBeDeletedStore = fromConstCharArrayToCharPtrArray("-e", " file <- readRDS(\"" + filePath.native() + "\");"
			+ "if(is.null(file$Built)){ q(\"no\",-1); };"
			+ "file$DESCRIPTION[\"Version\"];"
			+ "file$DESCRIPTION[\"Package\"];"
			+ "file$Built$R;"
			+ "q(\"no\",0)"
		);

		char* mutableArgv[3] = { localToBeDeletedStore[0].get(),localToBeDeletedStore[1].get(), nullptr };
		auto rpkg = spawnReadOnlyProcess(backend::Rpkg::executablePath, mutableArgv, 2, []() {setlocale(LC_ALL, "C.UTF-8"); });
		
		std::unique_ptr<FILE, decltype(&fclose)> in{ fdopen(rpkg.stdoutFD.get(), "r") , fclose };
		rpkg.stderrFD.reset();


		backend::RpkgPackage result;

		parsingState state = versionHeader;
		char* buffer = nullptr;
		
		size_t bufferSize = 0;
		while (size_t nrRead = getdelim(&buffer, &bufferSize, '\n', in.get())) {
			if (nrRead == -1) {
				break;
			}
			std::u8string_view data{ (const char8_t*)buffer,nrRead };

			switch (state)
			{
			case parsingState::entry://unused if using Rscript
				if (data == u8"\n") {
					state = versionHeader;
				}
				else {
					state = error;
				}
				break;
			case parsingState::versionHeader:
				if (trim(std::move(data), u8" \n\t") == u8"Version") {
					state = version;
				}
				else {
					state = error;
				}
				break;
			case parsingState::version:
				result.packageVersion = trim(std::move(data), u8" \n\t\""); //".*"\n
				if (data.empty()) {
					state = error;
				}
				else {
					state = packageHeader;
				}
				break;
			case parsingState::packageHeader:
				if (trim(std::move(data), u8" \n\t") == u8"Package") {
					state = package;
				}
				else {
					state = error;
				}
				break;
			case parsingState::package:
				result.packageName = trim(std::move(data), u8" \n\t\""); //".*"\n
				if (data.empty()) {
					state = error;
				}
				else {
					state = rVersion;
				}
				break;
			case parsingState::rVersion:
				data.remove_prefix(3);
				data = trim(std::move(data), u8"\xE2\x80\x98\xe2\x80\x99 \n\t\"\'\'"); //the amgic characters are ‘ and ’ cannot be inline unless we force the compiler to read them as utf-8
				result.rVersion = data;
				if (data.empty()) {
					state = error;
				}
				else {
					state = done;
				}
				break;
			case parsingState::done:
				state = data == u8"\n" ? parsingState::done : parsingState::error; //allow trailing newline;
			default:
				state = parsingState::error;
				break;
			}
		}
		if (buffer)
			free(buffer);

		auto waitResult = rpkg.close();


		if (!waitResult.has_value() || waitResult != 0) {
			return {};
		}

		if (state == parsingState::done) {
			return result;
		}
		else {
			return std::nullopt;
		}
	}

}

std::optional<backend::RpkgPackage> backend::Rpkg::resolvePathToPackage(const std::filesystem::path& fullpath)
{
	std::optional<RpkgPackage> found = std::nullopt;

	auto parent = fullpath.parent_path();
	for (auto lastPath = fullpath; parent != lastPath; lastPath = parent, parent = parent.parent_path()) {
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
			ToBeClosedFd fd = open(potentialPackageId.c_str(),O_RDONLY| O_NONBLOCK); //C++ does not allow opening files ore reading in a good unblocking mode... THis also does not guarantee non blocking bt if it were say a socket bound to the filesystem...
			char buffer[1];
			if (fd && read(fd.get(), &buffer, 1) == 1) {
				//thie file exists! yay. Now we invoke R to read the package information itself.
				//we hope that the first occurence will be the one we want.
				found = resolvePackageFromDescription(potentialPackageId);
			}
			//it is possible tor a package to be loaded without being installed. Sucha a package, however, is not suitable for automated downloading and running.
			//as such, 
			if (found != std::nullopt) {
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
