#pragma once
#include "../middleend/middleEnd.hpp"
#include "./dpkgResolver.hpp"
#include "./rpkgResolver.hpp"
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace backend {

struct Trace {
    std::vector<middleend::file_info> files;
    std::vector<std::string> env;
    std::vector<std::string> args;
    fs::path work_dir;
};

class DockerfileTraceInterpreter {

    Rpkg rpkg_resolver{};

    std::vector<middleend::file_info*> getUnmatchedFiles();
    std::vector<middleend::file_info*> getExecutedFiles();
    std::unordered_set<absFilePath> symlinkList();
    void persistDirectoriesAndSymbolicLinks(
        std::ostream& dockerImage, const std::filesystem::path& scriptLocation);

    Trace trace_;
    std::vector<DebPackage> debian_packages;

    void resolve_r_packages();
    void resolve_debian_packages();
    void resolve_ignored_files();

    void create_dockerfile();

  public:
    DockerfileTraceInterpreter(Trace const& trace) : trace_(trace) {}

    void finalize();
};
} // namespace backend
