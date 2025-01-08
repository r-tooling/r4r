#include "middleEnd.hpp"
#include "../cFileOptHelpers.hpp"
#include "../toBeClosedFd.hpp"
#include <cassert>
#include <fcntl.h>
#include <optional>

// stat
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {
std::optional<std::string>
tryReadShellbang(fileDescriptor fd) { // TODO: can the filename contain spaces?
    char shellbang[2];
    auto bytes = read(fd, shellbang, 2);
    if (bytes == 2 && shellbang[0] == '#' && shellbang[1] == '!') {
        std::string result;
        // we assume the name will be very short. No strbuf
        // essentially matching a #! ?([^ \t\n]*)
        constexpr int byteCount = 1024;
        char bytes[byteCount];
        bool terminated = false;
        bool first = true;
        while (!terminated) {
            auto nrRead = read(fd, bytes, byteCount);
            if (nrRead <= 0) // TODO: error handling
                break;
            terminated = terminated || nrRead != byteCount;
            std::string_view parsedStr(bytes, bytes + nrRead);

            if (first) {
                if (parsedStr[0] == ' ')
                    parsedStr = parsedStr.substr(1);
                first = false;
            }

            if (auto found = parsedStr.find_first_of(" \n\t");
                found != std::string_view::npos) {
                parsedStr = parsedStr.substr(0, found);
                terminated = true;
            }
            result += parsedStr;
        }
        return result;
    } else {
        return std::nullopt;
    }
}

std::filesystem::path
resolveToAbsolute_impl(const std::filesystem::path& base,
                       const std::filesystem::path& relativePath, bool log,
                       middleend::MiddleEndState::resolveFlagSet flags) {
    /*
    For POSIX-based operating systems, std::filesystem::absolute(p) is
    equivalent to std::filesystem::current_path() / p except for when p is the
    empty path.

    For Windows, std::filesystem::absolute may be implemented as a call to
    GetFullPathNameW.
    */
    if (flags & middleend::MiddleEndState::nofollow_simlink) {
        flags &= ~middleend::MiddleEndState::nofollow_simlink;
        auto lastElem =
            relativePath.filename(); // TODO: this only works sometimes!!!
        auto relativeRest = relativePath.parent_path();
        return resolveToAbsolute_impl(base, relativeRest, log, flags) /
               lastElem;
    } else {

        if (relativePath.is_absolute() && base != "") {
            return resolveToAbsolute_impl(
                "", relativePath, log,
                flags); // need to make it canonical still
        }

        std::error_code code;
        std::filesystem::path resultingPath =
            std::filesystem::canonical(base / relativePath, code);
        if (code) {
            if (log)
                fprintf(stderr,
                        "Cannot resolve path to %s as it fails to resolve "
                        "correctly, hoping a concatenation will not cause "
                        "further issues\n",
                        relativePath.c_str());
            resultingPath =
                std::filesystem::weakly_canonical(base / relativePath);
        }
        return resultingPath;
    }
}

} // namespace
namespace middleend {

MiddleEndState::running_thread_state&
MiddleEndState::pidToObj(const pid_t process) {
    auto val = processToInfo.find(process);
    assert(val != processToInfo.end());
    return val->second;
}
const MiddleEndState::running_thread_state&
MiddleEndState::pidToObj(const pid_t process) const {
    auto val = processToInfo.find(process);
    assert(val != processToInfo.end());
    return val->second;
}

MiddleEndState::MiddleEndState(absFilePath initialWorkdir, char* initialEnv[],
                               std::vector<std::string> args)
    : args{std::move(args)}, initialDir(initialWorkdir.native()) {
    auto ptr = std::unique_ptr<file_info>(
        new file_info{.realpath = initialWorkdir,
                      .accessibleAs = {},
                      .wasEverCreated = false,
                      .wasEverDeleted = false,
                      .isCurrentlyOnTheDisk = true,
                      .wasInitiallyOnTheDisk = true,
                      .type = file_info::dir,
                      .requiresAllSubEntities = std::nullopt});
    encounteredFilenames.emplace(initialWorkdir, std::move(ptr));
    while (initialEnv != nullptr && *initialEnv != nullptr) {
        env.emplace_back(*initialEnv);
        ++initialEnv;
    }
}

file_info*
MiddleEndState::createUnbackedFD(absFilePath&& filename,
                                 file_info::FileType type) {
    auto file = std::unique_ptr<file_info>(
        new file_info{.realpath = filename,
                      .accessibleAs = {},
                      .wasEverCreated = false,
                      .wasEverDeleted = false,
                      .isCurrentlyOnTheDisk = false,
                      .wasInitiallyOnTheDisk = false,
                      .type = type,
                      .requiresAllSubEntities = std::nullopt});
    auto raw = file.get();
    nonFileDescriptors.emplace_back(std::move(file));
    return raw;
}

file_info*
MiddleEndState::createErrorFD(const char* errorMessage) {
    fprintf(stderr, "%s", errorMessage);
    auto filepath = "unknownFD ERROR" + std::to_string(++errorCount);
    auto ptr = std::unique_ptr<file_info>(new file_info{
        .realpath = filepath,
        .accessibleAs = {},
        .wasEverCreated = std::nullopt,
        .wasEverDeleted = std::nullopt,
        .isCurrentlyOnTheDisk = std::nullopt,
        .wasInitiallyOnTheDisk = std::nullopt,
        .type = std::nullopt,
        .requiresAllSubEntities = std::nullopt,
    });
    auto raw = ptr.get();
    encounteredFilenames.emplace(filepath, std::move(ptr));
    return raw;
}

file_info*
MiddleEndState::tryFindFile(const absFilePath& filename) const {
    if (auto it = encounteredFilenames.find(filename);
        it != encounteredFilenames.end()) {
        auto& [_, info] = *it;
        return info.get();
    } else {
        return nullptr;
    }
}

void MiddleEndState::trackNewProcess(pid_t process) {
    auto val = processToInfo.find(process);
    if (val != processToInfo.end()) { // I have already been created by the
                                      // other call or existed in the past.
        if (val->second.exiting) {    // did I ever exist in the past?
            processToInfo.erase(val); // if so delete me and recreate
        } else {                      // I have accidentally been created twice.
            return;
        }
    }

    auto fdTable = std::make_shared<MiddleEndState::FD_Table>();
    FreeUniquePtr workdir{
        get_current_dir_name()}; // is there a better heuristic?

    processToInfo.emplace(
        process,
        MiddleEndState::running_thread_state{
            process,
            std::make_shared<FS_Info>(std::filesystem::path{workdir.get()}),
            std::move(fdTable)});
    if (!firstProcessInitialised) {
        auto in = createUnbackedFD("stdin", file_info::pipe);
        fdTable->table.emplace(0, in);
        auto out = createUnbackedFD("stdout", file_info::pipe);
        fdTable->table.emplace(1, out);
        auto err = createUnbackedFD("stderr", file_info::pipe);
        fdTable->table.emplace(2, err);

        firstProcessInitialised = true;
    }
}

void MiddleEndState::trackNewProcess(pid_t process, pid_t parent, bool copy,
                                     std::optional<pid_t> assumedChildPid,
                                     bool cloneFS) {
    if (assumedChildPid.has_value() && assumedChildPid != process) {
        // we assigned wrong!
        assert(false); // todo: error handling
    }
    if (processToInfo.find(process) == processToInfo.end())
        trackNewProcess(process); // TODO: dont create the FD table whichh will
                                  // only get replaced now

    auto& proc = pidToObj(process);
    auto& val = pidToObj(parent);

    // TODO: support unshare

    if (cloneFS) {
        proc.fsInfo = val.fsInfo;
    } else {
        proc.fsInfo = std::make_shared<FS_Info>(*val.fsInfo);
    }

    if (!copy) {
        proc.fdTable = val.fdTable; // copy ptr
    } else {
        proc.fdTable = std::make_shared<MiddleEndState::FD_Table>(
            *val.fdTable); // copy table itself
    }
}

void MiddleEndState::createDirectory(pid_t process, const absFilePath& filename,
                                     const relFilePath& relativePath) {
    auto& val = pidToObj(process);
    auto access = access_info{.pid = process,
                              .relPath = relativePath,
                              .flags = std::nullopt,
                              .executable = false,
                              .workdir = val.fsInfo->workdir};

    file_info* fileInfo = tryFindFile(filename);
    if (fileInfo) {
        if (fileInfo->isCurrentlyOnTheDisk) {
            fprintf(stderr, "Error in caching the filesystem image, new "
                            "directory assumed to already exist.\n");
        }
        fileInfo->registerAccess(std::move(access));
        fileInfo->type = file_info::dir; // TODO: historical data ever relevant?
        fileInfo->isCurrentlyOnTheDisk = true;
        fileInfo->wasEverCreated = true;

    } else {
        auto ptr = std::unique_ptr<file_info>(
            new file_info{.realpath = filename,
                          .accessibleAs = {std::move(access)},
                          .wasEverCreated = true,
                          .wasEverDeleted = false,
                          .isCurrentlyOnTheDisk = true,
                          .wasInitiallyOnTheDisk = false,
                          .type = file_info::dir,
                          .requiresAllSubEntities = std::nullopt});
        fileInfo = ptr.get();
        encounteredFilenames.emplace(filename, std::move(ptr));
    }
}

void MiddleEndState::removeDirectory(pid_t process,
                                     const absFilePath& filename) {
    auto& val = pidToObj(process);

    if (auto info = tryFindFile(filename)) {
        info->registerAccess(access_info{.pid = process,
                                         .relPath = filename,
                                         .flags = std::nullopt,
                                         .executable = false,
                                         .workdir = val.fsInfo->workdir});
        if (!info->isCurrentlyOnTheDisk) {
            fprintf(stderr, "Error in caching the filesystem image, directory "
                            "assumed to not exist.\n");
        }
        if (!info->type.has_value()) {
            info->type = file_info::dir;
        } else if (info->type != file_info::dir) {
            fprintf(stderr,
                    "rmdir succeeded on path %s but we expected the type not "
                    "to be dir but %d",
                    info->realpath.c_str(),
                    static_cast<int>(info->type.value()));
            info->type = file_info::dir;
        }
        info->wasEverDeleted = true;
        info->isCurrentlyOnTheDisk = false;
    } else {
        auto ptr = std::unique_ptr<file_info>(new file_info{
            .realpath = filename,
            .accessibleAs = {decltype(std::declval<file_info>()
                                          .accessibleAs)::value_type{
                process, filename, 0, false, val.fsInfo->workdir}},
            .wasEverCreated = false,
            .wasEverDeleted = true,
            .isCurrentlyOnTheDisk = false,
            .wasInitiallyOnTheDisk = true,
            .type = file_info::dir,
            .requiresAllSubEntities = false,
        });
        encounteredFilenames.emplace(filename, std::move(ptr));
    }
    // TODO:
    //  markDirectoryWritable(filename.parent_path());
    //  markDirectoriesSerchable(filename.parent_path());
}

void MiddleEndState::removeNonDirectory(pid_t process,
                                        const absFilePath& filename) {
    auto& val = pidToObj(process);

    auto access = access_info{.pid = process,
                              .relPath = filename,
                              .flags = std::nullopt,
                              .executable = false,
                              .workdir = val.fsInfo->workdir};

    if (auto info = tryFindFile(filename)) {
        info->registerAccess(std::move(access));
        if (!info->isCurrentlyOnTheDisk) {
            fprintf(stderr, "Error in caching the filesystem image, file "
                            "assumed to not exist.\n");
        }
        if (info->type == file_info::dir) {
            fprintf(stderr, "Error in caching the filesystem image, file "
                            "assumed to be a directory.\n");
            info->type = std::nullopt;
        }
        info->isCurrentlyOnTheDisk = false;
        info->wasEverDeleted = true;
    } else {
        auto ptr = std::unique_ptr<file_info>(
            new file_info{.realpath = filename,
                          .accessibleAs = {std::move(access)},
                          .wasEverCreated = false,
                          .wasEverDeleted = true,
                          .isCurrentlyOnTheDisk = false,
                          .wasInitiallyOnTheDisk = true,
                          .type = std::nullopt,
                          .requiresAllSubEntities = std::nullopt});
        encounteredFilenames.emplace(filename, std::move(ptr));
    }
}

void MiddleEndState::changeDirectory(
    pid_t process, const std::filesystem::path& newWorkingDirectory) {
    auto& val = pidToObj(process);
    auto result = newWorkingDirectory; // this makes the semantics of what
                                       // actually gets copied explicit. No need
                                       // to implicity copy in the argument
    result = resolveToAbsolute(process, newWorkingDirectory);
    val.fsInfo->workdir = std::move(result);
}

void MiddleEndState::changeDirectory(pid_t process,
                                     fileDescriptor fileDescriptor) {
    return changeDirectory(
        process,
        getFilePath<true>(process, fileDescriptor).value_or("/pathError"));
}

absFilePath MiddleEndState::resolveToAbsolute(
    pid_t process, const std::filesystem::path& relativePath,
    fileDescriptor fileDescriptor, bool log, resolveFlagSet flags) const {
    if (fileDescriptor == AT_FDCWD) {
        return resolveToAbsolute(process, relativePath, log, flags);
    }
    if (log) {
        return resolveToAbsolute_impl(
            getFilePath<true>(process, fileDescriptor).value_or(""),
            relativePath, log, flags);
    } else {
        return resolveToAbsolute_impl(
            getFilePath<false>(process, fileDescriptor).value_or(""),
            relativePath, log, flags);
    }
}

absFilePath
MiddleEndState::resolveToAbsolute(pid_t process,
                                  const std::filesystem::path& relativePath,
                                  bool log, resolveFlagSet flags) const {
    auto& val = pidToObj(process);
    return resolveToAbsolute_impl(val.fsInfo->workdir, relativePath, log,
                                  flags);
}

absFilePath MiddleEndState::resolveToAbsoluteDeleted(
    pid_t process, const std::filesystem::path& relativePath) const {
    if (relativePath.is_absolute()) {
        return relativePath;
    }
    auto& val = pidToObj(process);

    // https://en.cppreference.com/w/cpp/filesystem/absolute
    /*
    For POSIX-based operating systems, std::filesystem::absolute(p) is
    equivalent to std::filesystem::current_path() / p except for when p is the
    empty path.

    For Windows, std::filesystem::absolute may be implemented as a call to
    GetFullPathNameW.
    */
    if (relativePath == "" || relativePath == "./" || relativePath == ".") {
        return val.fsInfo->workdir;
    }
    return std::filesystem::weakly_canonical(val.fsInfo->workdir / relativePath)
        .lexically_normal();
}

void MiddleEndState::openHandling(pid_t process, absFilePath filename,
                                  relFilePath relativePath, fileDescriptor fd,
                                  int flags,
                                  std::optional<statResults> statInfo) {
    auto& val = pidToObj(process);
    auto access = access_info{.pid = process,
                              .relPath = relativePath,
                              .flags = flags,
                              .executable = false,
                              .workdir = val.fsInfo->workdir};

    file_info* fileInfo = tryFindFile(filename);
    if (fileInfo) {
        fileInfo->registerAccess(std::move(access));
        if (fileInfo->isCurrentlyOnTheDisk != statInfo.has_value()) {
            fprintf(stderr, "When creating a file, a file was assumed to exist "
                            "when it did not or vice versa.\n");
            // TODO: don't fstat if we "know"
        }
        fileInfo->wasEverCreated = !statInfo.has_value();
    } else {
        auto ptr = std::unique_ptr<file_info>{new file_info{
            .realpath = filename,
            .accessibleAs = {std::move(access)},
            .wasEverCreated = !statInfo.has_value(),
            .wasEverDeleted = false,
            .isCurrentlyOnTheDisk = true,
            .wasInitiallyOnTheDisk = statInfo.has_value(),
            .type = std::nullopt, // could be literally anything. - stat?
            .requiresAllSubEntities = std::nullopt,
        }};
        if (statInfo.has_value()) {
            ptr->type = statInfo->type;
        }
        fileInfo = ptr.get();
        encounteredFilenames.emplace(filename, std::move(ptr));
    }
    val.registerFD(fd, fileInfo);
}

bool MiddleEndState::execFile(pid_t process, absFilePath filename,
                              relFilePath relativePath, size_t depth,
                              bool overrideFailed) // TODO: Binfmt_misc
{
    // TODO: what if we first open a file before executing it? - shebangs fail
    // TODO: what if we execute a file, change it and then proceed to execute it
    // again? - shebangs fail
    bool doRegisterAccess = overrideFailed;
    bool failed = false;

    auto& val = pidToObj(process);
    auto access = access_info{.pid = process,
                              .relPath = relativePath,
                              .flags = std::nullopt,
                              .executable = true,
                              .workdir = val.fsInfo->workdir};

    if (auto info = tryFindFile(filename)) {
        info->registerAccess(std::move(access));
    } else {
        auto ptr = std::unique_ptr<file_info>(new file_info{
            .realpath = filename,
            .accessibleAs = {std::move(access)},
            .wasEverCreated = false,
            .wasEverDeleted = false,
            .isCurrentlyOnTheDisk = true,
            .wasInitiallyOnTheDisk = true,
            .type = file_info::file, // TODO: The kernel allow for executing say
                                     // a block device... It is extremely
                                     // unklikely, though.
            .requiresAllSubEntities = std::nullopt,
        });

        ToBeClosedFd fd{open(filename.c_str(), O_RDONLY)};

        // todo: add checks for
        if (!fd) {
            if (!overrideFailed)
                return true;
            else {
                encounteredFilenames.emplace(filename, std::move(ptr));
                return true;
            }
        }
        struct stat statData;
        auto statOK = fstat(fd.get(), &statData);
        if (!statOK || (!((S_IXUSR | S_IXGRP | S_IXOTH) &
                          statData.st_mode))) { // todo: keep track of context
                                                // for permissions.
            if (!overrideFailed) {
                return true;
            }
        }

        if (auto str = tryReadShellbang((fileDescriptor)fd);
            str.has_value()) { // TODO: this is essentially cached for further
                               // calls. Consider detecting if a given file was
                               // written since it had been executed and if it
                               // were, this needs to be redone.
            if (depth > 4) {
                return true;
            }

            absFilePath path{str.value()};
            // https://github.com/torvalds/linux/blob/2c71fdf02a95b3dd425b42f28fd47fb2b1d22702/fs/exec.c#L1903
            // https://github.com/torvalds/linux/blob/2c71fdf02a95b3dd425b42f28fd47fb2b1d22702/fs/exec.c#L1803
            // the kernel treats this as just about any other binary rewrite.
            // these are asuumed to be executed from the current wokr dir but I
            // ahve not managed to find much info on that aside from random
            // stack overflow threads.

            // todo: testme for symlink detection
            // for recursion the kernel has a hard limit- has a hard limit see
            // for example
            // https://github.com/SerenityOS/serenity/blob/ee3dd7977d4c88c836bfb813adcf3e006da749a8/Kernel/Syscalls/execve.cpp#L881
            // or the bin rewrite limit in linux.
            failed = execFile(process, resolveToAbsolute(process, path), path,
                              depth + 1, overrideFailed);
            doRegisterAccess = doRegisterAccess || !failed;
        }
        if (doRegisterAccess)
            encounteredFilenames.emplace(filename, std::move(ptr));
    }
    return failed;

    // todo search perms for directories
}

void MiddleEndState::closeFileDescriptor(pid_t process, fileDescriptor fd) {
    auto& val = pidToObj(process);
    val.fdTable->table.erase(fd);
}
void MiddleEndState::listDirectory(pid_t process, fileDescriptor fd) {
    auto& val = pidToObj(process);

    if (auto info = val.fdTable->table.find(fd);
        info != val.fdTable->table.end()) {
        info->second->type = file_info::dir; // add warn
        info->second->requiresAllSubEntities =
            true; // todo: be more specific about which files are required.
    } else {
        val.registerFD(
            fd, createErrorFD("Listing a directory not previously opened.\n"));
    }
}

/*
 * basically a
 * newfd = oldFd
 */
void MiddleEndState::registerFdAlias(pid_t process, fileDescriptor newFd,
                                     fileDescriptor oldFD) {
    auto& val = pidToObj(process);
    if (auto oldFile = val.fdTable->table.find(oldFD);
        oldFile != val.fdTable->table.end()) {
        auto& [oldFd, fileInfo] = *oldFile;
        val.registerFD(newFd, fileInfo);
    } else {
        auto fileInfo = createErrorFD(
            "creating a duplicate of an unresolved file descriptor\n");
        val.registerFD(oldFD, fileInfo);
        val.registerFD(newFd, fileInfo);
    }
}

void MiddleEndState::toBeDeleted(pid_t process) {
    auto& val = pidToObj(process);
    val.exiting = true;
}

void MiddleEndState::registerPipe(pid_t process, fileDescriptor pipes[2]) {
    auto& val = pidToObj(process);
    auto pipe = ++this->pipeCount;
    auto out =
        createUnbackedFD("pipe_read_" + std::to_string(pipe), file_info::pipe);
    val.registerFD(pipes[0], out);

    auto in =
        createUnbackedFD("pipe_write_" + std::to_string(pipe), file_info::pipe);
    val.registerFD(pipes[1], in);
}

void MiddleEndState::registerSocket(pid_t process, fileDescriptor socket) {
    auto& val = pidToObj(process);
    auto socketNr = ++this->socketCount;
    auto file = createUnbackedFD("socket_" + std::to_string(socketNr),
                                 file_info::socket);
    val.registerFD(socket, file);
}

void MiddleEndState::registerSocket(pid_t process, fileDescriptor sockets[2]) {
    auto socketNr = ++this->socketCount;

    auto& val = pidToObj(process);
    {
        auto file = createUnbackedFD(
            "socket_pair_1_" + std::to_string(socketNr), file_info::socket);
        val.registerFD(sockets[0], file);
    }
    {
        auto file = createUnbackedFD(
            "socket_pair_2_" + std::to_string(socketNr), file_info::socket);
        val.registerFD(sockets[1], file);
    }
}

void MiddleEndState::registerProcessFD(pid_t process, pid_t otherProcess,
                                       fileDescriptor procFD) {
    auto& val = pidToObj(process);
    auto processNR = ++this->processFD;
    auto file = createUnbackedFD("process_" + std::to_string(otherProcess) +
                                     "_" + std::to_string(processNR),
                                 file_info::process);
    val.registerFD(procFD, file);
}

void MiddleEndState::registerTimer(pid_t process, fileDescriptor timerFd) {
    auto& val = pidToObj(process);
    auto count = ++this->timerCount;
    auto file =
        createUnbackedFD("timer_" + std::to_string(count), file_info::clock);
    val.registerFD(timerFd, file);
}

void MiddleEndState::registerEpoll(pid_t process, fileDescriptor epollFD) {
    auto& val = pidToObj(process);
    auto count = ++this->epollCount;
    auto file =
        createUnbackedFD("epoll_" + std::to_string(count), file_info::epoll);
    val.registerFD(epollFD, file);
}

void MiddleEndState::registerEventFD(pid_t process, fileDescriptor eventFD) {
    auto& val = pidToObj(process);
    auto count = ++this->eventCount;
    auto file =
        createUnbackedFD("event_" + std::to_string(count), file_info::eventFD);
    val.registerFD(eventFD, file);
}
std::optional<MiddleEndState::statResults>
MiddleEndState::checkFileInfo(const absFilePath& abs) const {
    using FT = file_info::FileType;
    auto found = tryFindFile(abs);
    if (found) {
        if (!found->isCurrentlyOnTheDisk.value_or(true)) {
            return std::nullopt;
        }
        if (found->type.has_value()) {
            return statResults{found->type.value()};
        }
    }
    struct stat data;
    int state;
    int err = 0;
    // if we wanted to follow symbolic links, it should be done via the path
    // itself.
    state = fstatat(AT_FDCWD, abs.c_str(), &data, AT_SYMLINK_NOFOLLOW);
    err = errno;
    (void)err;
    assert(state == 0 ||
           (state = -1 && err == ENOENT)); // todo: handle other errors
    if (state == 0) {
        FT type;
        switch (data.st_mode & S_IFMT) {
        case S_IFBLK:
            type = FT::blockDev;
            break;
        case S_IFCHR:
            type = FT::charDev;
            break;
        case S_IFDIR:
            type = FT::dir;
            break;
        case S_IFIFO:
            type = FT::pipe;
            break;
        case S_IFLNK:
            type = FT::link;
            break;
        case S_IFREG:
            type = FT::file;
            break;
        case S_IFSOCK:
            type = FT::socket;
            break;
        default:
            type = FT::other;
            break;
        }

        if (found) {
            found->type = type;
        }
        return statResults{type};
    } else {
        if (err != ENOENT) {
            fprintf(stderr, "Error stating file %s errno: %d", abs.c_str(),
                    err);
        }
        return std::nullopt;
    }
}
void MiddleEndState::syscallWarn(int nr, const char* message) {
    if (syscallWarnings.emplace(nr).second) {
        fprintf(stderr, "%s\n", message);
    }
}

} // namespace middleend