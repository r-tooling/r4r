#include "./dpkgResolver.hpp"
#include "../processSpawnHelper.hpp"
#include "../stringHelpers.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>
#include <cassert>

//STAT
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
namespace {

	struct resolvedData {
		//the name of the package as resolved by dpkg
		std::vector<std::u8string> packageName;
		/*
			the name of the filename which was resolved
			we may sometimes try heuristics for the filename
			/usr/bin/uname
			resolves to nothing
			* /uname*
			resolves to two packages
		*/

		std::filesystem::path actualFile;
	};

	/** parsing the following
	*
	* https://www.man7.org/linux/man-pages/man1/dpkg-query.1.html
	*        -S, --search filename-search-pattern...
		   Search for packages that own files corresponding to the given
		   patterns.  Standard shell wildcard characters can be used in
		   the pattern, where asterisk (*) and question mark (?) will
		   match a slash, and backslash (\) will be used as an escape
		   character.

		   If the first character in the filename-search-pattern is none
		   of ‘*[?/’ then it will be considered a substring match and
		   will be implicitly surrounded by ‘*’ (as in *filename-search-
		   pattern*).  If the subsequent string contains any of ‘*[?\’,
		   then it will handled like a glob pattern, otherwise any
		   trailing ‘/’ or ‘/.’ will be removed and a literal path
		   lookup will be performed.

		   This command will not list extra files created by maintainer
		   scripts, nor will it list alternatives.

		   The output format consists of one line per matching pattern,
		   with a list of packages owning the pathname separated by a
		   comma (U+002C ‘,’) and a space (U+0020 ‘ ’), followed by a
		   colon (U+003A ‘:’) and a space, followed by the pathname. As
		   in:

			 pkgname1, pkgname2: pathname1
			 pkgname3: pathname2

		   File diversions are printed with the following localized
		   strings:

			 diversion by pkgname from: diverted-from
			 diversion by pkgname to: diverted-to

		   or for local diversions:

			 local diversion from: diverted-from
			 local diversion to: diverted-to

		   Hint: When machine parsing the output, it is customary to set
		   the locale to C.UTF-8 to get reproducible results.
	*/

	std::vector<resolvedData> resolveString(const std::string& param) {
		using std::operator""sv;
		//format("{} -S '{}'")

		//I want these to get cleaned up automatically and yet need raw pointers. Honestly a vector of strings is how this should be done.

		auto localToBeDeletedStore = fromConstCharArrayToCharPtrArray("-S", param);

		char* mutableArgv[3] = { localToBeDeletedStore[0].get(),localToBeDeletedStore[1].get(), nullptr };

		auto dpkgProcess = spawnReadOnlyProcess(backend::Dpkg::executablePath, mutableArgv, 2, []() {
			setlocale(LC_ALL, "C.UTF-8");
			});

		std::unique_ptr<FILE, decltype(&fclose)> in{ fdopen(dpkgProcess.stdoutFD.get(), "r") , fclose };
		dpkgProcess.stderrFD.reset();



		std::vector<resolvedData> results;

		char* buffer = nullptr;
		size_t bufferSize = 0;
		while (size_t nrRead = getdelim(&buffer, &bufferSize, '\n', in.get())) {
			if (nrRead == -1) {
				break;
			}
			std::vector<std::u8string> packages;
			std::u8string_view data{ (const char8_t*)buffer,nrRead };
			//needs c++ 23 and maybe newer gcc version than 11...
			/*
			for (const auto package : std::views::split(data, ","sv)) {
				std::string_view packageSV( package );
			}
			*/
			size_t begin = 0;
			size_t end = 0;
			/*
			* ensure the last test was not the end of the input else read the following
			* packagename may include a :!!!
			* packagename [,packagename]: filePath
			*/
			while (end != nrRead && end != data.npos) {
				end = data.find(u8", "sv, begin);
				auto split = data.substr(begin, end);
				assert(split.size() > 0);
				if (end == nrRead || end == data.npos) {
					//the last part thereby packagename: filepath
					auto pos = split.rfind(u8": "sv); //last of and hoping the filename does not contain :. TODO: count them maybe?
					auto potentialSv = std::u8string_view{ split };
					if (pos != split.npos) {  //if not : is found we assume a mistake and print the entire output as the filepath and package both
						//we have found a potentiall delimiter for the path
						potentialSv.remove_prefix(pos); //remove the potential package name
						potentialSv.remove_prefix(2); //remove the : and whitespace around
						potentialSv.remove_suffix(1); //newline

						split = split.substr(0, pos); //we know how long the package name was.
					}

					packages.emplace_back(trim(std::move(split)));
					results.emplace_back(std::move(packages), potentialSv);
					break;
				}
				else {
					packages.emplace_back(trim(std::move(split)));
					begin = end + 2;
				}
			}
		};


		/*TODO: solve
		diversion by dash from: /bin/sh
diversion by dash to: /bin/sh.distrib
dash

		*/
		if (buffer)
			free(buffer);
		in.reset();
		
		auto processRes = dpkgProcess.close();
		return processRes == 0 ? results : std::vector<resolvedData>{};
	}

	struct context {
		const std::filesystem::path searchedFileName;
		struct stat statInfo;
		bool exists;

		context(const std::filesystem::path& path) : searchedFileName(path) {
			auto result = stat(path.native().c_str(), &statInfo);
			exists = result == 0;//TODO: error for access rights.
		}

	};

	template<class T, class Callback>
	std::optional<T> optOR(std::optional<T>&& value, Callback&& callback) {
		if (value.has_value()) {
			return value;
		}
		else {
			return callback();
		}
	};

	template<class T, class Callback, class ResultType = std::invoke_result_t<Callback, T> >
	std::optional<ResultType> optTransform(std::optional<T>&& value, Callback&& callback) {
		if (value.has_value()) {
			return callback(value.value());
		}
		else {
			return std::nullopt;
		}
	};

	std::optional<std::u8string> tryResolveVector(std::vector<resolvedData>&& potentialValues, context& context) {

		for (auto& fileInfo : potentialValues) {
			if (fileInfo.actualFile == context.searchedFileName) {
				//we have found the exact filename with path we are looking for. It shall to be the one. 
				return fileInfo.packageName[0];//TODO: what if we get multiple packages? TODO: can that happen for non-directories?
			}
			else {
				struct stat otherFileInfo;
				auto ret = stat(fileInfo.actualFile.native().c_str(), &otherFileInfo);
				if (ret == 0 && context.exists && context.statInfo.st_ino == otherFileInfo.st_ino) {
					//if the inode matches, we have found it. This also resolves symbolic links including the last as I follow links when calling stat()
					return fileInfo.packageName[0];
				}
			}
			//otherwise it is a dud;
		}
		return std::nullopt;
	}

	std::u8string resolveNameToVersion(const std::u8string& name) {
		using std::operator""sv;

		//this is a hack because c++ and utf8 sucks
		std::string_view nameSV{ reinterpret_cast<const char*>(name.data()), name.size() };

		auto localToBeDeletedStore = fromConstCharArrayToCharPtrArray("-s", nameSV);

		char* mutableArgv[3] = { localToBeDeletedStore[0].get(),localToBeDeletedStore[1].get(), nullptr };

		auto dpkgProcess = spawnReadOnlyProcess(backend::Dpkg::executablePath, mutableArgv, 2, []() {
			setlocale(LC_ALL, "C.UTF-8");
			});
		std::unique_ptr<FILE, decltype(&fclose)> in{ fdopen(dpkgProcess.stdoutFD.get(), "r") , fclose };
		dpkgProcess.stderrFD.reset();

		std::u8string result;
		char* buffer = nullptr;
		size_t bufferSize = 0;
		while (size_t nrRead = getdelim(&buffer, &bufferSize, '\n', in.get())) {
			if (nrRead == -1) {
				break;
			}
			std::u8string_view data{ (const char8_t*)buffer,nrRead };
			auto start = u8"Version:"sv;
			if (data.starts_with(start)) {
				data.remove_prefix(start.size());
				return std::u8string{ trim(std::move(data), u8" \n\t\"") };
			}
		}
		return u8"ERROR_NOT_FOUND";
		//we do not care for the return code here.
	}

}
//todo: only pass references but that has issues with optionals...
const backend::DpkgPackage& backend::Dpkg::nameToObject(const std::u8string& name) {
	if (auto find = packageNameToData.find(name); find != packageNameToData.end()) {
		return find->second;
	}
	else {
		auto emplaced = packageNameToData.try_emplace(name, name, resolveNameToVersion(name));
		return emplaced.first->second;
	}
}


std::optional<backend::DpkgPackage> backend::Dpkg::resolvePathToPackage(std::filesystem::path path)
{
	context con{ path };
	return 
		optTransform(
		optOR(
		tryResolveVector(resolveString(path.generic_string()), con), //try the package full path
		[&]() {
			return optOR(
				tryResolveVector(resolveString("*/" + path.filename().native()), con), //try just the package name 
				[&]() {
					return tryResolveVector(resolveString("*/" + path.filename().native()+".*"), con);//try just the package name	
				}
			);
		}
	), [&](std::u8string name) {return this->nameToObject(name); });
}