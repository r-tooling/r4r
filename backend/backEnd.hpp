#pragma once
#include "../common.hpp"
#include "../middleend/middleEnd.hpp"
#include "dpkgResolver.hpp"
#include "rpkgResolver.hpp"
#include <ostream>
#include <string>
#include <unordered_set>

namespace backend {

struct Trace {
    std::vector<middleend::file_info> files;
    std::unordered_map<std::string, std::string> env;
    std::vector<std::string> cmd;
    fs::path work_dir;
};

class DockerfileTraceInterpreter {

    Rpkg rpkg_resolver{};

    std::vector<middleend::file_info*> getUnmatchedFiles();
    std::vector<middleend::file_info*> getExecutedFiles();
    std::unordered_set<absFilePath> symlinkList();
    void persistDirectoriesAndSymbolicLinks(std::ostream& dockerImage,
                                            const fs::path& scriptLocation);

    Trace trace_;
    std::unordered_set<DebPackage> debian_packages;

    void resolve_r_packages();
    void resolve_debian_packages();
    void resolve_ignored_files();
    void set_environment_variables(std::ofstream& df);

    void create_dockerfile();

  public:
    DockerfileTraceInterpreter(Trace const& trace) : trace_(trace) {}

    void finalize();
    void copy_unmatched_files(std::ofstream& df, const fs::path& archive);
    void install_debian_packages(std::ofstream& df);
};
} // namespace backend
