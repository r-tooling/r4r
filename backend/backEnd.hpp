#pragma once
#include "../middleend/middleEnd.hpp"
#include "./dpkgResolver.hpp"
#include "./rpkgResolver.hpp"
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace backend {

class CachingResolver {

    Rpkg rpkgResolver{};
    Dpkg dpkgResolver{};

    std::vector<middleend::MiddleEndState::file_info*> getUnmatchedFiles();
    std::vector<middleend::MiddleEndState::file_info*> getExecutedFiles();
    std::unordered_set<absFilePath> symlinkList();
    void persistDirectoriesAndSymbolicLinks(
        std::ostream& dockerImage, const std::filesystem::path& scriptLocation);

  public:
    const decltype(middleend::MiddleEndState::encounteredFilenames)& files;
    const std::vector<std::string> args;
    const std::vector<std::string> env;
    const std::filesystem::path programWorkdir;

    CachingResolver(const decltype(files)& files, std::vector<std::string> env,
                    std::vector<std::string> args, std::filesystem::path dir)
        : files(files), args(args), env(env), programWorkdir(dir) {}
    void resolveRPackages();
    void resolveDebianPackages();
    /*
            This endpoint generates a csv list of accessed files and a script
       for creating a docker container
    */
    void csv(absFilePath output);
    void report(absFilePath output);
    void dockerImage(absFilePath output, const std::string_view tag);
};
} // namespace backend