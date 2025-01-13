#include "backEnd.hpp"
#include "../common.hpp"
#include "../csv/serialisedFileInfo.hpp"
#include "../util.hpp"
#include "dpkgResolver.hpp"
#include "rpkgResolver.hpp"

#include <fcntl.h> //O_* flags
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <sys/stat.h> //mkdirat
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct SubdirIterator {
    std::filesystem::path pathToWalk;

    struct dummy {};

    struct IterImpl {
        const std::filesystem::path& origPath;
        std::filesystem::path::iterator currentPos;
        std::filesystem::path soFar;

        IterImpl(const std::filesystem::path& origPath)
            : origPath{origPath}, currentPos(origPath.begin()),
              soFar{currentPos != origPath.end() ? *currentPos : ""} {};

        const auto& operator*() { return soFar; }

        auto operator++() {
            ++currentPos;
            if (currentPos != origPath.end())
                soFar /= *currentPos;
            return this;
        }

        auto operator!=(const dummy&) { return currentPos != origPath.end(); }
    };

    auto begin() { return IterImpl(pathToWalk); }
    auto end() { return dummy{}; }
};

enum fileAccessFlags { read = 1, write = 2, execute = 4 };

template <class T>
T&& replaceFirst(T&& source, const T&& from, const T&& to) {
    using noref = std::remove_reference_t<T>;
    noref newString{};
    newString.reserve(source.length()); // avoids a few memory allocations

    typename noref::size_type lastPos = 0;
    typename noref::size_type findPos;

    if (noref::npos != (findPos = source.find(from, lastPos))) {
        newString.append(source, lastPos, findPos - lastPos);
        newString += to;
        lastPos = findPos + from.length();
    }

    // Care for the rest after last occurrence
    newString += source.substr(lastPos);

    source.swap(newString);
    return std::forward<T>(source);
}

// std::string flags_to_str(int flags) {
//     std::string res = "";
//     if (flags & fileAccessFlags::read) {
//         res += "R";
//     }
//     if (flags & fileAccessFlags::write) {
//         res += "W";
//     }
//     if (flags & fileAccessFlags::execute) {
//         res += "X";
//     }
//     return res;
// }

// void writeScriptLaunch(std::ostream& str,
//                        const backend::DockerfileTraceInterpreter& state) {
//     str << "#!/bin/sh" << std::endl;
//     str << "cd '" << state.programWorkdir << "'" << std::endl; //
//     todo:escaping
//     // todo switch to user
//
//     for (auto& env : state.env) {
//         str << "export '" << env << "'" << std::endl;
//     }
//     bool first = true;
//     for (auto& arg : state.args) {
//         if (first) {
//             first = false;
//             str << arg << " ";
//         } else {
//             str << "'" << arg << "' ";
//         }
//     }
//     str << std::endl;
// }

// std::filesystem::path
// createLaunchScript(const std::filesystem::path& where,
//                    const backend::DockerfileTraceInterpreter& state) {
//     std::filesystem::path resPath = where / "launch.sh";
//     std::ofstream file{resPath,
//                        std::ios::openmode::_S_trunc |
//                            std::ios::openmode::_S_out}; // todo: file
//                            collision?
//     writeScriptLaunch(file, state);                     // todo: error?
//     return resPath;
// }
// std::string escapeForCSV(std::u8string unescaped) {
//     using std::string_literals::operator""s;
//     //	if (unescaped.contains(strType{ "," })) { c++23
//     if (unescaped.find(u8",") != unescaped.npos) {
//         auto s = u8"\""s +
//                  CSV::replaceAll(std::move(unescaped), u8"\""s, u8"\"\"\""s)
//                  + u8"\""s;
//         return std::string{reinterpret_cast<const char*>(s.data()),
//         s.length()};
//     } else {
//         return std::string{reinterpret_cast<const char*>(unescaped.data()),
//                            unescaped.length()};
//     }
// }

template <class Key, class Result>
std::optional<Result>
optionalResolve(const std::unordered_map<Key, Result>& container,
                const Key& key) {
    if (auto ptr = container.find(key); ptr != container.end())
        return ptr->second;
    else
        return std::nullopt;
}

void appendSymlinkResult(std::unordered_set<absFilePath>& resultStore,
                         std::filesystem::path symlink,
                         std::unordered_set<absFilePath> ignoreEqual) {
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
// void unpackFiles(
//     std::unordered_map<absFilePath, std::string>& dockerPathTranslator,
//     std::ofstream& dockerImage) {
//     if (!dockerPathTranslator.empty()) {
//         dockerImage << "ADD DockerData /tmp/DockerData" << std::endl;
//
//         dockerImage << "RUN "; // unpack
//         for (auto& [path, key] : dockerPathTranslator) {
//             dockerImage << "mv /tmp/DockerData/" << key << " " << path
//                         << " && ";
//         }
//         dockerImage << "true" << std::endl;
//     }
// }

// static void createBuildScript(
//     std::ofstream& dockerBuildScript,
//     std::unordered_map<absFilePath, std::string>& dockerPathTranslator,
//     const std::string_view& tag, std::ofstream& runDockerScript,
//     std::unordered_set<std::filesystem::path> unignoredFiles) {
//     std::ofstream dockerignore{".dockerignore"};
//     dockerignore << "*" << std::endl;
//     dockerignore << "!DockerData" << std::endl;
//     for (auto& item : unignoredFiles) {
//         dockerignore << "!" << item.filename().string() << std::endl;
//     }
//
//     dockerBuildScript << "#!/bin/sh" << std::endl
//                       << "mkdir 'DockerData'; cd 'DockerData'; ";
//     for (const auto& [source, dest] : dockerPathTranslator) {
//         dockerBuildScript
//             << "cp " << source << " " << dest
//             << std::endl; // todo: escaping, ln if the file is on the same
//                           // filesystem. Or amybe create a condition if ln
//                           // fails, fall back to cp.
//     }
//
//     dockerBuildScript << "cd ..;";
//     dockerBuildScript << "docker build -t '" << tag
//                       << "' -f dockerImage .; success=$?;" << std::endl;
//     dockerBuildScript << "rm -rf 'DockerData';" << std::endl;
//     dockerBuildScript << "if [ $success -eq 0 ]; then ./runDocker.sh; fi;"
//                       << std::endl;
//
//     runDockerScript << "docker run  -it --entrypoint bash " << tag <<
//     std::endl;
// }

} // namespace
namespace backend {

// FIXME: encapsulate the Dockerfile builder in a class which
// when outputted can format it

// FIXME: test symlinks - accessing a file via a symlink
// FIXME: test accessing a file via $HOME
// FIXME: test acessing a file via group permissions

void DockerfileTraceInterpreter::set_locale(std::ofstream& df) {
    std::optional<std::string> lang = "C"s;
    if (auto it = trace_.env.find("LANG"); it != trace_.env.end()) {
        lang = it->second;
        trace_.env.erase(it);
    }
    if (lang) {
        df << "ENV LANG=" << *lang << "\n\n"
           << "RUN apt-get update -y && \\\n"
           << "    apt-get install -y locales && \\\n"
           << "    locale-gen $LANG && \\\n"
           << "    dpkg-reconfigure locales\n\n";
    }
}

void DockerfileTraceInterpreter::create_dockerfile() {
    auto df = std::ofstream{"Dockerfile", std::ios::trunc | std::ios::out};

    // TODO: shall we upgrade? To 24.04 the latest LTS?
    // We could, but either we need to have a corresponding dev environment
    df << "FROM ubuntu:22.04"
       << "\n\n";

    set_locale(df);
    install_debian_packages(df);

    // install R packages
    rpkg_resolver.persist(df, "install-r-packages.R");

    copy_unmatched_files(df, "archive.tar");

    set_environment_variables(df);

    df << "RUN mkdir -p " << trace_.work_dir << "\n";
    df << "WORKDIR " << trace_.work_dir << "\n\n";

    // exec
    df << "CMD ";
    auto args = trace_.cmd | std::views::transform(util::escape_cmd_arg);
    util::print_collection(df, args, " ");
    df << "\n";

    df.close();
}

void DockerfileTraceInterpreter::set_environment_variables(std::ofstream& df) {
    if (trace_.env.empty()) {
        return;
    }

    static const std::unordered_set<std::string> ignored_env = {
        "DBUS_SESSION_BUS_ADDRES",
        "GPG_TTY",
        "HOME",
        "LOGNAME",
        "OLDPWD",
        "PWD",
        "SSH_AUTH_SOCK",
        "SSH_CLIENT",
        "SSH_CONNECTION",
        "SSH_TTY",
        "USER",
        "XDG_RUNTIME_DIR",
        "XDG_SESSION_CLASS",
        "XDG_SESSION_ID",
        "XDG_SESSION_TYPE"};

    std::vector<std::string> env;
    env.reserve(trace_.env.size());

    for (auto const& [k, v] : trace_.env) {
        if (!ignored_env.contains(k)) {
            env.push_back(STR(k << "=\"" << v << "\""));
        }
    }

    std::sort(env.begin(), env.end());

    df << "ENV \\\n  ";
    util::print_collection(df, env, " \\\n  ");
    df << "\n\n";
}

void DockerfileTraceInterpreter::install_debian_packages(std::ofstream& df) {
    // FIXME: the main problem with this implementation is that
    // it ignores the fact a package can come from multiple repos
    // and that these repos need to be installed.

    if (debian_packages.empty()) {
        return;
    }

    std::vector<DebPackage> packages(debian_packages.begin(),
                                     debian_packages.end());

    std::sort(packages.begin(), packages.end(),
              [](const DebPackage& a, const DebPackage& b) {
                  return std::tie(a.name, a.version) <
                         std::tie(b.name, b.version);
              });

    df << "ENV DEBIAN_FRONTEND=noninteractive\n";
    df << "RUN apt-get update -y && \\\n";
    df << "    apt-get install -y \\\n";

    for (size_t i = 0; i < packages.size(); ++i) {
        auto const& pkg = packages[i];
        df << "      " << pkg.name << "=" << pkg.version;
        if (i < packages.size() - 1) {
            df << " \\\n";
        }
    }
    df << "\n\n";
}

void DockerfileTraceInterpreter::copy_unmatched_files(std::ofstream& df,
                                                      fs::path const& archive) {
    std::vector<fs::path> unmatched_files;
    unmatched_files.reserve(trace_.files.size());

    for (auto const& f : trace_.files) {
        auto const& path = f.realpath;

        std::cout << "resolving: " << path << " to: ";

        if (!f.wasInitiallyOnTheDisk) {
            std::cout << "ignore - was not originally on the disk\n";
            continue;
        }

        if (!fs::exists(path)) {
            std::cout << "ignore - no longer exists\n";
            continue;
        }

        if (!fs::is_regular_file(path)) {
            std::cout << "ignore - not a regular file\n";
            continue;
        }

        std::ifstream i{path, std::ios::in};
        if (!i) {
            std::cout << "ignore - cannot by opened\n";
            continue;
        }

        std::cout << "copy\n";
        unmatched_files.push_back(path);
    }

    if (!unmatched_files.empty()) {
        util::create_tar_archive(archive, unmatched_files);
        df << "COPY [" << archive << ", " << archive << "]\n";
        df << "RUN tar xfv " << archive << " --absolute-names && rm -f "
           << archive << "\n";
        df << "\n";
    }
}

// void DockerfileTraceInterpreter::dockerImage(absFilePath output,
//                                   const std::string_view tag) {
//     using std::string_literals::operator""s;
//
//     auto dockerImage =
//         std::ofstream{output.parent_path() / "dockerImage",
//                       std::ios::openmode::_S_trunc |
//                           std::ios::openmode::_S_out}; // c++
//                           23noreplace
//     auto dockerBuildScript =
//         std::ofstream{output.parent_path() / "buildDocker.sh",
//                       std::ios::openmode::_S_trunc |
//                           std::ios::openmode::_S_out}; // c++
//                           23noreplace
//     auto runDockerScript = std::ofstream{
//         output.parent_path() / "runDocker.sh",
//         std::ios::openmode::_S_trunc | std::ios::openmode::_S_out};
//         //
//         TODO:
//
//     dockerImage << "FROM ubuntu:22.04" << std::endl;
//
//     // FIXME:
//     // dpkgResolver.persist(dockerImage);
//     // this is done before the R packages to ensure all  library
//     paths are
//     // accessible
//     std::filesystem::path directoryCreator{output.parent_path() /
//                                            "recreateDirectories.sh"};
//     persistDirectoriesAndSymbolicLinks(dockerImage,
//     directoryCreator);
//
//     std::filesystem::path rpkgCreator{output.parent_path() /
//                                       "installRPackages.sh"};
//     rpkgResolver.persist(dockerImage, rpkgCreator);
//
//     // COPY src dest does not work as it quickly exhaust the maximum
//     layer depth
//     // instead a folder with all the relevant data is serialised to
//     and then
//     // added to the image where it is unserialised.
//
//     // PERSIST FILES
//
//     std::unordered_map<absFilePath, std::string>
//     dockerPathTranslator;
//
//     auto file = createLaunchScript(".", *this);
//     dockerPathTranslator.emplace(std::filesystem::canonical(file),
//                                  std::to_string(dockerPathTranslator.size()));
//     for (auto info : getUnmatchedFiles()) {
//         if (isSpecialCased(info->realpath)) {
//             continue;
//         }
//         if (std::filesystem::is_regular_file(info->realpath)) {
//             dockerPathTranslator.emplace(
//                 info->realpath,
//                 std::to_string(dockerPathTranslator.size()));
//         }
//         // TODO: error on non symlink/dir/file
//     }
//
//     unpackFiles(dockerPathTranslator, dockerImage);
//
//     // PERSIST ENV
//
//     for (auto& e : env) {
//         dockerImage << "ENV " << replaceFirst(std::string(e), "="s,
//         "=\""s)
//                     << "\"" << std::endl;
//     }
//     dockerImage << "WORKDIR " << programWorkdir << std::endl;
//
//     createBuildScript(dockerBuildScript, dockerPathTranslator, tag,
//                       runDockerScript, {directoryCreator,
//                       rpkgCreator});
// }

//
// std::vector<middleend::MiddleEndState::file_info*>
// DockerfileTraceInterpreter::getUnmatchedFiles() {
//     std::vector<middleend::MiddleEndState::file_info*> res{};
//     for (auto& [path, info] : files) {
//
//         // FIXME: dpkg
//         // auto dpkg = optionalResolve(dpkgResolver.resolvedPaths,
//         path)
//         //                 .value_or(std::nullopt);
//         auto rpkg = optionalResolve(rpkgResolver.resolvedPaths, path)
//                         .value_or(std::nullopt);
//         // FIXME:
//         if (/*!dpkg &&*/ !rpkg) {
//             res.emplace_back(info.get());
//         }
//     }
//     return res;
// }
//
// std::vector<middleend::MiddleEndState::file_info*>
// DockerfileTraceInterpreter::getExecutedFiles() {
//     std::vector<middleend::MiddleEndState::file_info*> res{};
//     for (auto& [path, info] : files) {
//         for (auto& val : info.get()->accessibleAs) {
//             if (val.executable) {
//                 res.emplace_back(info.get());
//                 break; // inner loop only
//             }
//         }
//     }
//     return res;
// }

/*
Package repositories may "own" files under both the fullpath and a
symlinked path /usr/lib/a -> /lib/a -> we may only ever see one of these
accessed. As such, the current "dumb" way of matching files has to stay.

but it can be done in batches.

*/

std::unordered_set<absFilePath> DockerfileTraceInterpreter::symlinkList() {

    std::unordered_set<absFilePath> currentList;
    // Resolving symlinks
    // take all the non-realpath ways to access this item
    for (auto& info : trace_.files) {
        for (auto& access : info.accessibleAs) {

            // if the file path is an absolute path
            auto path = std::filesystem::path{};
            if (access.relPath.is_absolute()) {
                path = access.relPath.lexically_normal();
            } else {
                path = (access.workdir / access.relPath).lexically_normal();
            }
            if (path != info.realpath) {
                if (std::filesystem::is_symlink(path)) {
                    appendSymlinkResult(currentList, path, {info.realpath});
                }
                currentList.emplace(std::move(path));
            }
        }
    }
    return currentList;
}

// void DockerfileTraceInterpreter::persistDirectoriesAndSymbolicLinks(
//     std::ostream& dockerImage, const std::filesystem::path&
//     scriptLocation) { std::unordered_set<std::filesystem::path>
//     resolvedPaths;
//
//     std::ofstream result{scriptLocation, std::ios::openmode::_S_trunc
//     |
//                                              std::ios::openmode::_S_out};
//
//     // TODO: add a method for detecting that a sublink here belongs
//     to a package
//     // and mark it as resolved. but this requires resolving all the
//     symlinks
//     // which are to be created to their real paths as we go along.
//     auto all = symlinkList();
//     for (const auto& [path, _] : files) {
//         all.emplace(path);
//     }
//     // These should not actually be required but better safe than
//     sorry in this
//     // case. Any found library should have been detected before.
//     all.merge(rpkgResolver.getLibraryPaths());
//     all.emplace(programWorkdir);
//     for (decltype(auto) path : all) {
//         // do not persist links which will be persisted by dpkg.
//         bool ignoreFinal = false;
//         // FIXME:
//         // if (auto found = dpkgResolver.resolvedPaths.find(path);
//         //     found != dpkgResolver.resolvedPaths.end()) {
//         //     ignoreFinal = found->second.has_value();
//         // }
//
//         for (const auto& segment : SubdirIterator(path)) {
//             if (isSpecialCased(segment) || (ignoreFinal && segment ==
//             path))
//             {
//                 continue;
//             }
//             if (!resolvedPaths.contains(segment)) {
//                 if (std::filesystem::is_directory(segment)) {
//                     result
//                         << "mkdir -p " << segment
//                         << std::endl; //-p is there just to be
//                         absolutely sure
//                                       // everything works. could be
//                                       ommited
//                 } else if (std::filesystem::is_symlink(segment)) {
//                     result << "ln -s " <<
//                     std::filesystem::read_symlink(segment)
//                            << " " << segment << std::endl;
//                 }
//                 resolvedPaths.emplace(segment);
//             }
//         }
//     }
//     if (!resolvedPaths.empty()) {
//         dockerImage << "COPY [" << scriptLocation << "," <<
//         scriptLocation
//                     << "]" << std::endl;
//         dockerImage << "RUN bash " << scriptLocation << " || true"
//                     << std::endl; // we always want this to complete
//                     even if the
//                                   // directories did not get created
//     }
// }

// FIXME: simplify the R package resolution
// The main assumption here is that there is just one R, and we know which one
// and how it is called. This is an OK assumption IMHO (eventually, it could be
// parameterized). Then this could be simpler. We could ask R which are the
// library locations, and only resolve packages from there. It does not make
// sense to consider any other files.
void DockerfileTraceInterpreter::resolve_r_packages() {
    auto links = symlinkList();
    if (!rpkg_resolver.areDependenciesPresent()) {
        fprintf(stderr, "Unable to resolve R packages as the required "
                        "dependencies are not present\n");
        return;
    }

    auto op = [&](middleend::file_info const& f) -> bool {
        // always resolve all dependencies, the time
        // overlap is marginal and I need to know the
        // version even in dpkg packages.
        return rpkg_resolver.resolvePathToPackage(f.realpath).has_value();
    };

    std::erase_if(trace_.files, op);

    // FIXME: what to do with symlinks?
    // // Resolving symlinks
    // // take all the non-realpath ways to access this item
    // for (auto& info : symlinkList()) {
    //     rpkgResolver.resolvePathToPackage(info);
    // }
}

void DockerfileTraceInterpreter::resolve_ignored_files() {
    static util::FilesystemTrie<bool> ignored{false};
    if (ignored.is_empty()) {
        ignored.insert("/dev", true);
        ignored.insert("/etc/ld.so.cache", true);
        ignored.insert("/etc/nsswitch.conf", true);
        ignored.insert("/etc/passwd", true);
        ignored.insert("/proc", true);
        ignored.insert("/sys", true);
        // created by locale-gen
        ignored.insert("/usr/lib/locale/locale-archive", true);
        // fonts should be installed from a package
        ignored.insert("/usr/local/share/fonts", true);
        // this might be a bit too drastic, but cache is usually not
        // transferable anyway
        ignored.insert("/var/cache", true);
    }

    std::erase_if(trace_.files, [&](middleend::file_info const& f) {
        auto const& path = f.realpath.string();
        if (ignored.find_last_matching(path)) {
            std::cout << "resolving: " << path << " to: ignored" << std::endl;
            return true;
        }
        return false;
    });

    // ignore the .uuid files from fontconfig
    static const std::unordered_set<fs::path> fontconfig_dirs = {
        "/usr/share/fonts", "/usr/share/poppler", "/usr/share/texmf/fonts"};

    std::erase_if(trace_.files, [&](middleend::file_info const& f) {
        auto const& path = f.realpath;
        for (auto const& d : fontconfig_dirs) {
            if (util::is_sub_path(path, d)) {
                if (path.filename() == ".uuid") {
                    return true;
                }
            }
        }
        return false;
    });
}

void populate_root_symlinks(std::unordered_map<fs::path, fs::path>& symlinks) {
    fs::path root = "/";
    for (const auto& entry : fs::directory_iterator(root)) {
        if (entry.is_symlink()) {
            std::error_code ec;
            fs::path target = fs::read_symlink(entry.path(), ec);
            if (!target.is_absolute()) {
                target = fs::canonical(root / target);
            }
            if (!ec && fs::is_directory(target)) {
                symlinks[entry.path()] = target;
            }
        }
    }
}

std::vector<fs::path> get_root_symlink(const fs::path& path) {
    static std::unordered_map<fs::path, fs::path> symlinks;
    if (symlinks.empty()) {
        populate_root_symlinks(symlinks);
    }

    std::vector<fs::path> result = {path};

    for (const auto& [symlink, target] : symlinks) {
        if (util::is_sub_path(path, target)) {
            fs::path candidate = symlink / path.lexically_relative(target);

            std::error_code ec;
            if (fs::exists(candidate, ec) &&
                fs::equivalent(candidate, path, ec)) {
                result.push_back(candidate);
                break;
            }
        }
    }

    return result;
}

void DockerfileTraceInterpreter::resolve_debian_packages() {
    auto dpkg = DpkgDatabase::from_path();

    auto has_resolved = [&](middleend::file_info const& f) -> bool {
        auto& path = f.realpath;
        for (auto& p : get_root_symlink(path)) {
            if (auto* resolved = dpkg.lookup_by_path(p); resolved) {
                std::cout << "resolving: " << path << " to: " << resolved->name
                          << std::endl;
                debian_packages.insert(*resolved);
                return true;
            }
        }
        return false;
    };

    std::erase_if(trace_.files, has_resolved);
}

void DockerfileTraceInterpreter::finalize() {
    resolve_debian_packages();
    resolve_r_packages();
    resolve_ignored_files();

    create_dockerfile();
}

} // namespace backend
