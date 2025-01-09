#include "rpkgResolver.hpp"
#include "../cFileOptHelpers.hpp"
#include "../processSpawnHelper.hpp"
#include "../stringHelpers.hpp"
#include <cassert>
#include <fcntl.h>
#include <fstream>

namespace {
/*
heuristics - description "License: Part of R 4.4.0", "Version: 4.4.0" where
the number fits the version.install.packages("tcltk") -> Warning message:
package �tcltk� is a base package, and should not be updated
the packages are also located in the default R install dir.*/
static const std::unordered_set<std::u8string_view> basePackages = {
    u8"base",     u8"datasets", u8"utils",   u8"grDevices", u8"tools",
    u8"graphics", u8"compiler", u8"stats",   u8"methods",   u8"grid",
    u8"parallel", u8"stats4",   u8"splines", u8"tcltk",

};

/*
run
file
<-readRDS("/home/adamep/R/x86_64-pc-linux-gnu-library/4.3/sass/Meta/package.rds")
file$DESCRIPTION["Version"]
        Version
        "0.4.8"
file$DESCRIPTION["Package"]
        Package
        "sass"
file$Built$R
[1] �4.3.3�
*/

std::optional<backend::RpkgPackage>
resolvePackageFromDescription(const std::filesystem::path& filePath) {
    ArgvWrapper argv{"-e",
                     " file <- readRDS(\"" + filePath.native() +
                         "\");"
                         "if(is.null(file$Built)){ q(\"no\",-1); };"
                         "file$DESCRIPTION[\"Version\"];"
                         "file$DESCRIPTION[\"Package\"];"
                         "file$Built$R;"
                         "unique(append(append(names(file$Imports),names(file$"
                         "Depends)),names(file$LinkingTo))); 'printedAll';"
                         "q(\"no\",0);"};

    auto rpkg =
        spawnStdoutReadProcess(backend::Rpkg::executablePath, argv,
                               []() noexcept { setlocale(LC_ALL, "C.UTF-8"); });

    enum {
        entry,
        versionHeader,
        version,
        packageHeader,
        package,
        rVersion,
        depends,
        done,
        error,
    } state = versionHeader;

    std::u8string packageVersion, packageName, parsedRVersion;
    std::unordered_set<std::u8string> dependsList;

    for (auto buffer : LineIterator{rpkg.out.get()}) {

        std::u8string_view data{(const char8_t*)buffer.data(), buffer.size()};

        switch (state) {
        case entry: // unused if using Rscript
            if (data == u8"\n") {
                state = versionHeader;
            } else {
                state = error;
            }
            break;
        case versionHeader:
            if (trim(data, u8" \n\t") == u8"Version") {
                state = version;
            } else {
                state = error;
            }
            break;
        case version:
            packageVersion = trim(data, u8" \n\t\""); //".*"\n
            if (data.empty()) {
                state = error;
            } else {
                state = packageHeader;
            }
            break;
        case packageHeader:
            if (trim(data, u8" \n\t") == u8"Package") {
                state = package;
            } else {
                state = error;
            }
            break;
        case package:
            packageName = trim(data, u8" \n\t\""); //".*"\n
            if (data.empty()) {
                state = error;
            } else {
                state = rVersion;
            }
            break;
        case rVersion:
            data.remove_prefix(3);
            data = trim(
                data,
                u8"\xE2\x80\x98\xe2\x80\x99 \n\t\"\'\'"); // the magic
                                                          // characters are �
                                                          // and � cannot be
                                                          // inline unless we
                                                          // force the compiler
                                                          // to read them as
                                                          // utf-8
            parsedRVersion = data;
            if (data.empty()) {
                state = error;
            } else {
                state = depends;
            }
            break;
        case depends:
            trim(data); // dont care for newline
            if (data == u8"character(0)" ||
                data == u8"NULL") { // one of the potential empty cases. Will
                                    // till be continued by a printedAll
                break;
            }

            // remove \[[0-9]+\] or I could split on space and discard the first
            ltrim(data, u8" [");
            ltrim(data, u8" 0123456789");
            ltrim(data, u8" ] ");
            if (data == u8"\"printedAll\"") { // my custom output
                state = done;
                break;
            }
            for (auto item : explodeMul(data, u8" ")) {
                trim(item, u8"\xE2\x80\x98\xe2\x80\x99 \n\t\"\'\'");
                if (!item.empty() && !basePackages.contains(item))
                    dependsList.emplace(item);
            }
            break;
        case done:
            state = data == u8"\n" ? done : error; // allow trailing newline;
            break;
        default:
            state = error;
            break;
        }
    }

    if (!rpkg.close().terminatedCorrectly()) {
        return std::nullopt;
    }

    if (state == done) {
        return backend::RpkgPackage{
            packageName,
            packageVersion,
            parsedRVersion,
            dependsList,
            filePath.parent_path()
                .parent_path(), // pckage.rds -> META -> folder
            basePackages.contains(packageName)};
    } else {
        return std::nullopt;
    }
}

std::optional<backend::RpkgPackage>
exactPathToPackage(std::filesystem::__cxx11::path& parent) {
    auto potentialPackageId = (parent / "Meta" / "package.rds");
    // this mirrors what R does
    // https://github.com/wch/r-source/blob/trunk/src/library/base/R/library.R
    //   pfile <- system.file("Meta", "package.rds", package = package,
    //                        lib.loc = which.lib.loc)
    // if(!nzchar(pfile)) error //cannot be empty
    //  then read the description. Which internally checks that the Build value
    //  is set.
    if (std::filesystem::exists(potentialPackageId)) {
        ToBeClosedFd fd{open(
            potentialPackageId.c_str(),
            O_RDONLY |
                O_NONBLOCK)}; // C++ does not allow opening files ore reading in
                              // a good unblocking mode... THis also does not
                              // guarantee non blocking bt if it were say a
                              // socket bound to the filesystem...
        char buffer[1];

        if (fd && read(fd.get(), &buffer, 1) == 1) {
            // thie file exists! yay. Now we invoke R to read the package
            // information itself. we hope that the first occurence will be the
            // one we want.
            return resolvePackageFromDescription(potentialPackageId);
        }
        // it is possible tor a package to be loaded without being installed.
        // Sucha a package, however, is not suitable for automated downloading
        // and running.
    }
    return std::nullopt;
}
} // namespace

std::optional<const backend::RpkgPackage*>
backend::Rpkg::resolvePathToPackage(const std::filesystem::path& fullpath) {
    std::optional<const RpkgPackage*> found = std::nullopt;

    bool isDir = std::filesystem::is_directory(fullpath);
    bool first = true;
    auto parent = isDir ? fullpath : fullpath.parent_path();
    for (std::filesystem::path lastPath = fullpath;
         parent != lastPath || (isDir && first);
         lastPath = parent, parent = parent.parent_path()) {
        first = false;
        if (auto resolved = resolvedPaths.find(parent);
            resolved != resolvedPaths.end()) {
            found = resolved->second;
            break;
        }
        auto findRes = exactPathToPackage(parent);
        if (findRes) {
            found = &*packageNameToData.emplace(findRes.value()).first;
            break;
        }
    }
    // we assume all things in subdirectories of a package are a package in and
    // of itself. if nothing belongs to a package, the whole path can be marked
    // as such. this cannot be done for dpkg as for example colacles folder is
    // created by multiple packages but the files inside are clearly separated.
    resolvedPaths.try_emplace(parent, found);
    for (decltype(auto) part : std::filesystem::relative(fullpath, parent)) {
        if (part != "") { // todo: am I needed?
            resolvedPaths.try_emplace(parent /= part, found);
        }
    }
    return found;
}

std::unordered_set<std::filesystem::path> backend::Rpkg::getLibraryPaths() {
    std::unordered_set<std::filesystem::path> paths;
    for (auto& package : packageNameToData) {
        paths.insert(package.whereLocated.parent_path());
    }
    return paths;
}

bool backend::Rpkg::areDependenciesPresent() {
    // Rscript --help. I could just check that the file exists somewhere in path
    // but that would involve path variable resolution and this lets the stdlib
    // handle things.
    return checkExecutableExists(backend::Rpkg::executablePath,
                                 ArgvWrapper{"--help"});
}

// this is assumed to be only called after we have resolved all other files.

backend::TopSortIterator backend::Rpkg::topSortedPackages() noexcept {
    // emplace may cause rehas and this iterator invalidation, so the entire
    // thing is done in batches.
    RpkgSet AllPackages = packageNameToData;
    { // TODO: multiple R versions break this. Hard. Only the first instance of
      // a package is considered...
        RpkgSet toAdd;
        bool toAddEmpty = false;
        do {
            for (const auto& package : AllPackages) {
                for (const auto& deps : package.dependsOn) {
                    if (!AllPackages.contains(deps) && !toAdd.contains(deps)) {
                        toAdd.emplace(resolvePackageToName_noinsert(deps));
                    }
                }
            }
            toAddEmpty = toAdd.empty();
            AllPackages.merge(std::move(toAdd));
        } while (!toAddEmpty);
    } // end lifetime guard

    return TopSortIterator{AllPackages};
}

void backend::Rpkg::persist(std::ostream& dockerImage,
                            const std::filesystem::path& scriptLocation) {

    // TODO:
    /// usr/lib/R/etc/Renviron.site could get modified. So could probably any
    /// other file in dpkg packages...
    // this can be done before the program is ever run
    //  a good solution should check file hashes.

    // todo: check R version for individual libraries for consistency's sake.
    if (!packageNameToData.empty()) {
        std::ofstream result{scriptLocation, std::ios::openmode::_S_trunc |
                                                 std::ios::openmode::_S_out};

        dockerImage << "COPY [" << scriptLocation << ", " << scriptLocation
                    << "]" << std::endl;
        dockerImage << "RUN Rscript " << scriptLocation << std::endl;
        dockerImage << "\n";
        // this will break if ran directly due to too large a string argument. I
        // do not know the specifics but passing it into a file and executing
        // the file works.

        // this way the require should not conflict with the installed version.
        result << "cores <- min(parallel::detectCores(), 4)"
               << std::endl // parallel is a part of the core
               << "tmpDir <- tempdir()" << std::endl
               << "install.packages('remotes', lib = c(tmpDir))" << std::endl
               << "require('remotes', lib.loc = c(tmpDir)) " << std::endl;

        // TODO: if we wanted to be fancy, instead get a vector of all the R
        // packages currently topsorted. Install these in parallel.
        // TODO: add parsing of the source param to get the potential github and
        // such install rather than this api.
        // TODO: add check that the version is detected here first

        for (auto& package : topSortedPackages()) {
            if (package.isBaseRpackage) {
                continue; // these cannot be installed
            }

            std::string version = toNormal(package.packageVersion);
            if (version != "NULL") {
                version = "'" + version + "'";
            }
            // this works due to a combination of things explained here
            // https://stat.ethz.ch/pipermail/r-devel/2018-October/076989.html
            // and here
            // https://stackoverflow.com/questions/17082341/installing-older-version-of-r-package
            result << "install_version('" << toNormal(package.packageName)
                   << "', " << version
                   << ", upgrade = 'never', dependencies = FALSE, Ncpus = cores"
                   << ")" << std::endl;
            // unfortunatelly upgrade=never is not sufficient
            //  the package may depend on other packages which are not installed
            //  and such packages would then get installed at possibly higher
            //  versions than intended that is why the topsorting happens
        }
    }
}

backend::RpkgPackage
backend::Rpkg::resolvePackageToName_noinsert(const std::u8string& packageName) {

    for (const auto& path : getLibraryPaths()) {
        // if meta and what not exists;
        auto attemptedResolve = resolvePathToPackage(
            path / packageName); // the dummy directory is there
        if (attemptedResolve.has_value() &&
            (*attemptedResolve)->packageName == packageName) {
            return **attemptedResolve;
        }
    }
    fprintf(stderr,
            "Was unable to find a required package %s this will result in a "
            "latest version installation. If you know the verion change the "
            "genrerated dockerfile accordingly.\n",
            toNormal(packageName).c_str());
    return backend::RpkgPackage{
        packageName, u8"NULL", u8"",
        {},          "",       basePackages.contains(packageName)};
}
