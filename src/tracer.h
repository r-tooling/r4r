#ifndef TRACER_H
#define TRACER_H

#include "archive.h"
#include "common.h"
#include "config.h"
#include "dockerfile.h"
#include "dpkg_database.h"
#include "file_tracer.h"
#include "filesystem_trie.h"
#include "logger.h"
#include "manifest.h"
#include "manifest_format.h"
#include "manifest_section.h"
#include "process.h"
#include "resolvers.h"
#include "rpkg_database.h"
#include "syscall_monitor.h"
#include "user.h"
#include "util.h"
#include "util_fs.h"
#include "util_io.h"

#include <filesystem>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include <unordered_map>
#include <utility>
#include <vector>

/// The global ignore file list.
static inline FileSystemTrie<bool> const kDefaultIgnoredFiles = [] {
    FileSystemTrie<bool> trie;
    trie.insert("/dev", true);
    trie.insert("/etc/ld.so.cache", true);
    trie.insert("/etc/nsswitch.conf", true);
    trie.insert("/etc/passwd", true);
    trie.insert("/proc", true);
    trie.insert("/sys", true);
    // created by locale-gen
    trie.insert("/usr/lib/locale/locale-archive", true);
    // fonts should be installed from a package
    trie.insert("/usr/local/share/fonts", true);
    // this might be a bit too drastic, but cache is usually not
    // transferable anyway
    trie.insert("/var/cache", true);
    return trie;
}();

struct Options {
    LogLevel log_level = LogLevel::Warning;
    fs::path R_bin{"R"};
    std::vector<std::string> cmd;
    std::string docker_base_image{"ubuntu:22.04"};
    std::string docker_image_tag{STR(kBinaryName << "/test")};
    std::string docker_container_name{STR(kBinaryName << "-test")};
    fs::path output_dir{"."};
    fs::path makefile;
    AbsolutePathSet results;
    bool docker_sudo_access{true};
    bool run_make{true};
    bool skip_manifest{false};
    // TODO: make this mutable so more files could be added from command line
    FileSystemTrie<bool> ignore_file_list = kDefaultIgnoredFiles;
};

struct TracerState {
    DpkgDatabase dpkg_database;
    RpkgDatabase rpkg_database;

    std::vector<FileInfo> traced_files;

    Manifest manifest;
};

class Task {
  public:
    explicit Task(std::string name) : name_(std::move(name)) {}

    virtual ~Task() = default;

    virtual void run(TracerState& state) = 0;

    virtual void stop() {}

    [[nodiscard]] std::string const& name() const { return name_; }

  private:
    std::string name_;
};

class TaskException : public std::runtime_error {
  public:
    explicit TaskException(std::string const& message)
        : std::runtime_error{message} {}
};

static constexpr std::string_view kDefaultTimezone{"UTC"};

class FileTracingTask : public Task {
  public:
    explicit FileTracingTask(FileSystemTrie<bool> const& ignore_file_list)
        : Task("Trace files"), ignore_file_list_{ignore_file_list} {}

    void run(TracerState& state) override;
    void stop() override;

  private:
    std::reference_wrapper<FileSystemTrie<bool> const> ignore_file_list_;
    SyscallMonitor* monitor_{};
};

inline void FileTracingTask::run(TracerState& state) {
    LOG(INFO) << "Tracing program: " << string_join(state.manifest.cmd, ' ');

    // silent the log while running the program not to interfere with the
    // output of the traced program
    auto old_log_sink = Logger::get().set_sink(std::make_unique<StoreSink>());

    FileTracer tracer{ignore_file_list_};
    SyscallMonitor monitor{state.manifest.cmd, tracer};
    monitor.redirect_stdout(std::cout);
    monitor.redirect_stderr(std::cerr);

    // this is just to support the stop()
    monitor_ = &monitor;
    auto [result, elapsed] = stopwatch([&] { return monitor_->start(); });
    monitor_ = nullptr;

    LOG(INFO) << "Finished tracing in " << format_elapsed_time(elapsed);
    LOG(INFO) << "Traced " << tracer.syscalls_count() << " syscalls and "
              << tracer.files().size() << " files";

    // print the postponed messages
    auto sink = Logger::get().set_sink(std::move(old_log_sink));
    auto const& events = dynamic_cast<StoreSink*>(sink.get())->get_messages();

    if (!events.empty()) {
        LOG(INFO) << "While tracing, there were " << events.size()
                  << " log event(s) captured during tracing";
        for (auto const& e : events) {
            Logger::get().log(e.to_log_event());
        }
    }

    switch (result.kind) {
    case SyscallMonitor::Result::Failure:
        throw TaskException("Failed to spawn the process");

    case SyscallMonitor::Result::Signal:
        throw TaskException(
            STR("Program was terminated by signal: " << *result.detail));

    case SyscallMonitor::Result::Exit:
        int exit_code = *result.detail;
        if (exit_code != 0) {
            throw TaskException(STR("Program exited with: " << exit_code));
        }

        auto file_map = tracer.files();
        auto& files = state.traced_files;
        files.reserve(file_map.size());
        for (auto const& [key, value] : file_map) {
            files.push_back(value);
        }

        std::sort(files.begin(), files.end(),
                  [](auto const& lhs, auto const& rhs) {
                      return lhs.path < rhs.path;
                  });
    }
}

inline void FileTracingTask::stop() {
    if (monitor_) {
        monitor_->stop();
    }
}

// We do not need this composition, each of the resolver can now be a task
class ResolveFileTask : public Task {
  public:
    ResolveFileTask(fs::path R_bin,
                    FileSystemTrie<bool> const& ignore_file_list)
        : Task("Resolve files"), R_bin_{std::move(R_bin)},
          ignore_file_list_{ignore_file_list} {}

    void run(TracerState& state) override;

  private:
    fs::path R_bin_;
    std::reference_wrapper<FileSystemTrie<bool> const> ignore_file_list_;
};

inline void ResolveFileTask::run(TracerState& state) {
    std::vector<std::pair<std::string, std::unique_ptr<Resolver>>> resolvers;

    resolvers.emplace_back(
        "ignore", std::make_unique<IgnoreFileResolver>(ignore_file_list_));

    resolvers.emplace_back(
        "deb", std::make_unique<DebPackageResolver>(state.dpkg_database));

    resolvers.emplace_back("R", std::make_unique<RPackageResolver>(
                                    state.rpkg_database, state.dpkg_database));

    resolvers.emplace_back("copy", std::make_unique<CopyFileResolver>());

    LOG(INFO) << "Resolving " << state.traced_files.size() << " files";

    std::string summary;
    size_t total_count = state.traced_files.size();

    for (auto& [name, resolver] : resolvers) {
        size_t count = state.traced_files.size();
        resolver->resolve(state.traced_files, state.manifest);
        summary += name + "(" +
                   std::to_string(count - state.traced_files.size()) + ") ";
    }

    LOG(INFO) << "Resolver summary: " << total_count << " file(s): " << summary;

    if (state.traced_files.empty()) {
        LOG(INFO) << "All files resolved";
    } else {
        LOG(INFO) << "Failed to resolve " << state.traced_files.size()
                  << " files";
        for (auto const& f : state.traced_files) {
            LOG(INFO) << "Failed to resolve: " << f.path;
        }
    }
}

class EditManifestTask : public Task {
  public:
    EditManifestTask() : Task("Edit manifest") {
        sections_.push_back(std::make_unique<CopyFilesManifestSection>());
    }

    void run(TracerState& state) override;

  private:
    static bool open_manifest(fs::path const& path);

    void load_manifest(std::istream& stream, Manifest& manifest);
    bool save_manifest(std::ostream& stream, Manifest const& manifest);

    std::vector<std::unique_ptr<ManifestSection>> sections_;
};

inline void EditManifestTask::load_manifest(std::istream& stream,
                                            Manifest& manifest) {
    ManifestFormat format;
    stream >> format;

    for (auto& section : sections_) {
        ManifestFormat::Section* input = format.get_section(section->name());
        if (input) {
            std::istringstream iss{input->content};
            section->load(iss, manifest);
        }
    }
}

inline bool EditManifestTask::save_manifest(std::ostream& stream,
                                            Manifest const& manifest) {
    ManifestFormat format;
    format.set_preamble(
        "This is the manifest file generated by R4R.\n"
        "You can update its content by either adding or "
        "removing/commenting lines in the corresponding sections.");

    bool any_content{false};
    for (auto& section : sections_) {
        std::ostringstream oss;
        bool content = section->save(oss, manifest);
        any_content |= content;

        if (!content) {
            continue;
        }

        format.add_section({.name = section->name(), .content = oss.str()});
    }

    if (any_content) {
        stream << format;
    }

    return any_content;
}

inline void EditManifestTask::run(TracerState& state) {
    auto manifest_file = TempFile{"r4r-manifest", ".conf"};
    bool any_content{false};
    {
        LOG(DEBUG) << "Saving manifest to: " << *manifest_file;
        std::ofstream stream{*manifest_file};
        any_content = save_manifest(stream, state.manifest);
    }

    if (!any_content) {
        LOG(DEBUG) << "Na manifest part need any edittting";
        return;
    }

    auto ts = fs::last_write_time(*manifest_file);

    if (open_manifest(*manifest_file) &&
        fs::last_write_time(*manifest_file) != ts) {
        LOG(DEBUG) << "Rereading manifest from: " << *manifest_file;
        std::ifstream stream{*manifest_file};
        load_manifest(stream, state.manifest);
    }
}

inline bool EditManifestTask::open_manifest(fs::path const& path) {
    char const* editor = std::getenv("VISUAL");
    if (editor == nullptr) {
        editor = std::getenv("EDITOR");
    }

    if (editor == nullptr) {
        LOG(WARN) << "Failed to open manifest: no editor found (set VISUAL "
                     "or EDITOR environment variable)";
        return false;
    }

    LOG(DEBUG) << "Opening the manifest file: " << path << " using " << editor;

    auto exit_code = Command{editor}.arg(path).spawn().wait();
    if (exit_code == -1) {
        LOG(ERROR) << "Failed to open the manifest file. Exit code: "
                   << exit_code;
        return false;
    }

    return true;
}

class ResolveRPackageSystemDependencies : public Task {
  public:
    ResolveRPackageSystemDependencies()
        : Task("Resolve R package system dependencies") {}

    void run(TracerState& state) override;
};

inline void ResolveRPackageSystemDependencies::run(TracerState& state) {
    auto& manifest = state.manifest;

    std::unordered_set<RPackage const*> compiled_packages;
    for (auto const* pkg :
         state.rpkg_database.get_dependencies(manifest.r_packages)) {
        if (pkg->is_base) {
            continue;
        }

        if (pkg->needs_compilation) {
            compiled_packages.insert(pkg);
            LOG(DEBUG) << "R package: " << pkg->name << " " << pkg->version
                       << " needs compilation";
        }

        manifest.r_packages.insert(pkg);
    }

    if (compiled_packages.empty()) {
        return;
    }

    LOG(INFO) << "There are " << compiled_packages.size()
              << " R packages that needs compilation, need to "
                 "pull system dependencies";

    auto deb_packages =
        RpkgDatabase::get_system_dependencies(compiled_packages);

    // bring in R headers and R development dependencies (includes
    // build-essential, gfortran, ...)
    deb_packages.insert("r-base-dev");

    for (auto const& name : deb_packages) {
        auto const* pkg = state.dpkg_database.lookup_by_name(name);
        if (pkg == nullptr) {
            LOG(WARN) << "Failed to find " << name
                      << " package needed "
                         "by R packages to be built from source";
        } else {
            auto it = manifest.deb_packages.insert(pkg);
            if (it.second) {
                LOG(DEBUG) << "Adding native dependency: " << pkg->name << " "
                           << pkg->version;
            }
        }
    }
}

class DockerFileBuilderTask : public Task {
  public:
    explicit DockerFileBuilderTask(fs::path output_dir, std::string base_image,
                                   bool docker_sudo_access)
        : Task("Create Dockerfile"), output_dir_{std::move(output_dir)},
          archive_{output_dir_ / "archive.tar"},
          permission_script_{output_dir_ / "permissions.sh"},
          cran_install_script_{output_dir_ / "install_r_packages.R"},
          dockerfile_{output_dir_ / "Dockerfile"},
          base_image_{std::move(base_image)},
          docker_sudo_access_{docker_sudo_access} {}

    void run(TracerState& state) override;

  private:
    static void install_deb_packages(DockerFileBuilder& builder,
                                     Manifest const& manifest);

    static void generate_permissions_script(std::vector<fs::path> const& files,
                                            std::ostream& out);

    static void set_lang_and_timezone(DockerFileBuilder& builder,
                                      Manifest const& manifest);

    static void set_environment(DockerFileBuilder& builder,
                                Manifest const& manifest);

    static void prepare_command(DockerFileBuilder& builder,
                                Manifest const& manifest);

    void copy_files(DockerFileBuilder& builder, Manifest const& manifest) const;

    void install_r_packages(DockerFileBuilder& builder,
                            Manifest const& manifest,
                            RpkgDatabase const& rpkg_database) const;

    void create_user(DockerFileBuilder& builder,
                     Manifest const& manifest) const;

    fs::path output_dir_;
    fs::path archive_;
    fs::path permission_script_;
    fs::path cran_install_script_;
    fs::path dockerfile_;
    fs::path base_image_;
    bool docker_sudo_access_{false};
};

inline void DockerFileBuilderTask::run(TracerState& state) {
    LOG(INFO) << "Generating Dockerfile: " << dockerfile_;

    DockerFileBuilder builder{base_image_, output_dir_};

    builder.env("DEBIAN_FRONTEND", "noninteractive");

    Manifest const& manifest{state.manifest};

    set_lang_and_timezone(builder, manifest);
    create_user(builder, manifest);

    install_deb_packages(builder, manifest);
    install_r_packages(builder, manifest, state.rpkg_database);
    copy_files(builder, manifest);

    set_environment(builder, manifest);
    prepare_command(builder, manifest);

    DockerFile docker_file = builder.build();
    docker_file.save(dockerfile_);
}

inline void DockerFileBuilderTask::copy_files(DockerFileBuilder& builder,
                                              Manifest const& manifest) const {

    std::vector<fs::path> files;
    for (auto const& [path, status] : manifest.copy_files) {
        if (status != FileStatus::Copy) {
            continue;
        }

        files.push_back(path);
        if (fs::is_symlink(path)) {
            files.push_back(fs::read_symlink(path));
        }
    }

    if (files.empty()) {
        return;
    }

    std::sort(files.begin(), files.end());

    create_tar_archive(archive_, files);
    // FIXME: the paths are not good, it will copy into out/archive.tar
    builder.copy({archive_}, archive_);

    builder.run({STR("tar -x -f "
                     << archive_
                     << " --same-owner --same-permissions --absolute-names"),
                 STR("rm -f " << archive_)});

    {
        std::ofstream permissions{permission_script_};
        generate_permissions_script(files, permissions);
    }

    // FIXME: the paths are not good, it will copy into out/...sh
    builder.copy({permission_script_}, permission_script_);
    builder.run({STR("bash " << permission_script_),
                 STR("rm -f " << permission_script_)});
}

inline void DockerFileBuilderTask::generate_permissions_script(
    std::vector<fs::path> const& files, std::ostream& out) {
    std::set<fs::path> directories;

    for (auto const& file : files) {
        fs::path current = file.parent_path();
        while (!current.empty() && current != "/") {
            directories.insert(current);
            current = current.parent_path();
        }
    }

    std::vector<fs::path> sorted_dirs(directories.begin(), directories.end());

    out << "#!/bin/bash\n\n";
    out << "set -e\n\n";

    for (auto const& dir : sorted_dirs) {
        struct stat info{};
        if (stat(dir.c_str(), &info) != 0) {
            LOG(WARN) << "Warning: Unable to access " << dir << '\n';
            continue;
        }

        uid_t owner = info.st_uid;
        gid_t group = info.st_gid;
        mode_t permissions = info.st_mode & 0777;

        out << "chown " << owner << ":" << group << " " << dir << '\n';
        out << "chmod " << std::oct << permissions << std::dec << " " << dir
            << '\n';
    }
}

inline void DockerFileBuilderTask::install_r_packages(
    DockerFileBuilder& builder, Manifest const& manifest,
    RpkgDatabase const& rpkg_database) const {
    if (manifest.r_packages.empty()) {
        return;
    }

    std::vector<RPackage const*> packages{manifest.r_packages.begin(),
                                          manifest.r_packages.end()};
    std::sort(
        packages.begin(), packages.end(),
        [](auto const* lhs, auto const* rhs) { return lhs->name < rhs->name; });

    auto all_packages = rpkg_database.get_dependencies(manifest.r_packages);

    std::ofstream script(cran_install_script_);

    // TODO: parameterize the max cores
    script << "options(Ncpus=min(parallel::detectCores(), 32))\n\n"
           << "tmp_dir <- tempdir()\n"
           << "install.packages('remotes', lib = c(tmp_dir))\n"
           << "require('remotes', lib.loc = c(tmp_dir))\n"
           << "on.exit(unlink(tmp_dir, recursive = TRUE))\n"
           << "\n\n"
           << "# install wrapper that turns warnings into errors\n"
           // clang-format off
        << "CHK <- function(thunk) {\n"
        << "  withCallingHandlers(force(thunk), warning = function(w) {\n"
        << "    if (grepl('installation of package ‘.*’ had non-zero exit status', conditionMessage(w))) { stop(w) }\n"
        << "  })\n"
        << "}\n\n"
           // clang-format on

           << "# installing packages\n\n";

    std::unordered_set<std::string> seen;
    for (auto const* pkg : all_packages) {
        if (pkg->is_base) {
            continue;
        }

        if (!seen.insert(pkg->name).second) {
            continue;
        }

        std::visit(
            overloaded{
                [&](RPackage::GitHub const& gh) {
                    script << "CHK(install_github('" << gh.org << "/" << gh.name
                           << "', ref = '" << gh.ref
                           << "', upgrade = 'never', dependencies = FALSE))\n";
                },
                [&](RPackage::CRAN const&) {
                    script << "CHK(install_version('" << pkg->name << "', '"
                           << pkg->version
                           << "', upgrade = 'never', dependencies = FALSE))\n";
                },
            },
            pkg->repository);
    }

    builder.copy({cran_install_script_}, "/");
    // TODO: simplify
    builder.run({STR("Rscript /" << cran_install_script_.filename().string()),
                 STR("rm -f /" << cran_install_script_.filename().string())});
}

inline void
DockerFileBuilderTask::set_lang_and_timezone(DockerFileBuilder& builder,
                                             Manifest const& manifest) {
    std::string lang = "C"s;
    if (auto it = manifest.envir.find("LANG"); it != manifest.envir.end()) {
        lang = it->second;
    }
    std::string tz = manifest.timezone;
    if (auto it = manifest.envir.find("TZ"); it != manifest.envir.end()) {
        tz = it->second;
    }

    builder.env("LANG", lang);
    builder.env("TZ", tz);
    builder.run({"apt-get update -y",
                 "apt-get install -y --no-install-recommends locales tzdata",
                 "echo $LANG >> /etc/locale.gen", "locale-gen $LANG",
                 "update-locale LANG=$LANG"});
}

inline void DockerFileBuilderTask::create_user(DockerFileBuilder& builder,
                                               Manifest const& manifest) const {
    std::vector<std::string> cmds;
    auto const& user = manifest.user;

    // create the primary group
    cmds.push_back(
        STR("groupadd -g " << user.group.gid << " " << user.group.name));

    // create groups
    for (auto const& group : user.groups) {
        cmds.push_back(STR("(groupadd -g " << group.gid << " " << group.name
                                           << " || groupmod -g " << group.gid
                                           << " " << group.name << ")"));
    }

    // prepare additional groups for `-G`
    std::vector<std::string> groups;
    groups.reserve(user.groups.size());
    for (auto const& group : user.groups) {
        groups.push_back(group.name);
    }
    std::sort(groups.begin(), groups.end());

    std::string group_list = string_join(groups, ',');

    // add user
    cmds.push_back(STR("useradd -u "
                       << user.uid << " -g " << user.group.gid
                       << (group_list.empty() ? "" : " -G " + group_list)
                       << " -d " << user.home_directory << " -s " << user.shell
                       << " " << user.username));

    // ensure home directory exists
    cmds.push_back(STR("mkdir -p " << user.home_directory));
    cmds.push_back(STR("chown " << user.username << ":" << user.group.name
                                << " " << user.home_directory));

    // sudo?
    if (docker_sudo_access_) {
        cmds.emplace_back("apt-get install -y sudo");
        cmds.push_back(STR("echo '"
                           << user.username
                           << " ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/"
                           << user.username));
        cmds.push_back(STR("chmod 0440 /etc/sudoers.d/" << user.username));
    }

    builder.run(cmds);
}

inline void DockerFileBuilderTask::set_environment(DockerFileBuilder& builder,
                                                   Manifest const& manifest) {
    auto& envir = manifest.envir;
    if (envir.empty()) {
        return;
    }

    // TODO: filtering should not be here
    static std::unordered_set<std::string> const ignored_env = {
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

    std::vector<std::string> sorted_env;
    sorted_env.reserve(envir.size());

    for (auto const& [k, v] : envir) {
        if (!ignored_env.contains(k)) {
            sorted_env.push_back(STR(k << "=\"" << v << "\""));
        }
    }

    std::sort(sorted_env.begin(), sorted_env.end());

    builder.env(sorted_env);
}

inline void DockerFileBuilderTask::prepare_command(DockerFileBuilder& builder,
                                                   Manifest const& manifest) {
    builder.run(
        {STR("mkdir -p " << manifest.cwd),
         STR("chown " << manifest.user.username << ":"
                      << manifest.user.group.name << " " << manifest.cwd)});
    builder.workdir(manifest.cwd);
    builder.user(manifest.user.username);
    builder.cmd(manifest.cmd);
}

inline void
DockerFileBuilderTask::install_deb_packages(DockerFileBuilder& builder,
                                            Manifest const& manifest) {
    // TODO: the main problem with this implementation is that
    // it ignores the fact a package can come from multiple repos
    // and that these repos need to be installed.

    if (manifest.deb_packages.empty()) {
        return;
    }

    std::vector<std::string> pkgs;
    std::unordered_set<std::string> seen;
    pkgs.reserve(manifest.deb_packages.size());

    for (auto const& pkg : manifest.deb_packages) {
        std::string p = pkg->name + "=" + pkg->version;
        if (seen.insert(p).second) {
            pkgs.push_back(p);
        }
    }

    std::sort(pkgs.begin(), pkgs.end());

    std::string line = string_join(pkgs, " \\\n      ");

    builder.run({
        "apt-get update -y",
        "apt-get install -y --no-install-recommends " + line,
    });
}

class MakefileBuilderTask : public Task {
  public:
    explicit MakefileBuilderTask(fs::path makefile,
                                 std::string docker_image_tag,
                                 std::string docker_container_name)
        : Task("Create Makefile"), makefile_{std::move(makefile)},
          docker_image_tag_{std::move(docker_image_tag)},
          docker_container_name_{std::move(docker_container_name)} {}

    void run(TracerState& state) override {
        std::ofstream stream{makefile_};

        generate_makefile(stream, state.manifest);

        LOG(INFO) << "Generated Makefile: " << makefile_;
    }

  private:
    void generate_makefile(std::ostream& makefile, Manifest const& manifest) {
        // TODO: make sure it executes in the same dir as the makefile
        // MAKEFILE_DIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))

        std::vector<fs::path> copy_files;
        for (auto const& [file, status] : manifest.copy_files) {
            if (status == FileStatus::Result) {
                copy_files.push_back(file);
            }
        }

        bool progress = check_docker_buildx();

        makefile << "IMAGE_TAG = " << docker_image_tag_ << "\n"
                 << "CONTAINER_NAME = " << docker_container_name_
                 << "\n"
                 // TODO: add to settings
                 << "TARGET_DIR = result"
                 << "\n\n"

                 << ".PHONY: all build run copy clean\n\n"

                 << "all: clean copy\n\n"

                 // clang-format off
                 << "build:\n"
                 << "\t@echo 'Building docker image $(IMAGE_TAG)'\n"
                 << "\t@docker build " << (progress ? "--progress=plain" : " ") << " -t $(IMAGE_TAG) . 2>&1"
                 << " | tee docker-build.log"
                 << "\n\n"
                 // clang-format on

                 // clang-format off
                 << "run: build\n"
                 << "\t@echo 'Running container $(CONTAINER_NAME)'\n"
                 << "\t@docker run -t --name $(CONTAINER_NAME) $(IMAGE_TAG) 2>&1"
                 << " | tee docker-run.log"
                 << "\n\n"
                 // clang-format on

                 << "copy: run\n"
                 // add a new line in case the docker run did
                 // not finish with one
                 << "\t@echo\n";

        if (copy_files.empty()) {
            makefile << "\t@echo 'No result files'\n";
        } else {
            makefile << "\t@echo 'Copying files'\n"
                     << "\t@mkdir -p $(TARGET_DIR)\n";

            for (auto const& file : copy_files) {
                makefile
                    << "\t@echo -n '  - " << file << "...'\n"
                    << "\t@docker cp -L $(CONTAINER_NAME):" << file.string()
                    << " $(TARGET_DIR) 2>/dev/null && echo ' done' || echo "
                       "' "
                       "failed'"
                    << "\n";
            }
        }

        makefile << "\n"
                 << "clean:\n"
                 << "\t@echo 'Cleaning previous container (if any)'\n"
                 << "\t-docker rm $(CONTAINER_NAME)\n"
                 << "\t@echo 'Cleaning previous image (if any)'\n"
                 << "\t-docker rmi $(IMAGE_TAG)\n"
                 << "\t@echo 'Cleaning previous result (if any)'\n"
                 << "\trm -rf $(TARGET_DIR)\n\n";
    }

    static bool check_docker_buildx() {
        // feels like monkey patching, but in the r2u,
        // the docker build does not support --progress
        auto res = Command{"docker"}.arg("build").arg("--help").output(true);
        if (res.exit_code != 0) {
            throw TaskException("Unable to run docker build --help");
        }

        return res.stdout_data.find("--progress") != std::string::npos;
    }

    fs::path makefile_;
    std::string docker_image_tag_;
    std::string docker_container_name_;
};

class RunMakefileTask : public Task {
  public:
    explicit RunMakefileTask(fs::path makefile)
        : Task("Run make"), makefile_{std::move(makefile)} {}

    RunMakefileTask(RunMakefileTask const&) = delete;
    RunMakefileTask& operator=(RunMakefileTask const&) = delete;

    void run([[maybe_unused]] TracerState& state) override {
        LOG(INFO) << "Running Makefile: " << makefile_;
        int exit_code = run_makefile_target("all", "make> ");
        if (exit_code != 0) {
            throw TaskException("Failed to run make");
        }
    }

  private:
    [[nodiscard]] int run_makefile_target(std::string const& target,
                                          std::string const& prefix) const {
        auto proc = Command("make")
                        .arg("-f")
                        .arg(makefile_.filename())
                        .arg(target)
                        .current_dir(makefile_.parent_path())
                        .set_stderr(Stdio::Merge)
                        .set_stdout(Stdio::Pipe)
                        .spawn();

        int fd = proc.stdout_fd();
        with_prefixed_ostream(std::cout, prefix,
                              [fd] { forward_output(fd, std::cout); });
        return proc.wait();
    }

    fs::path makefile_;
};

class CaptureEnvironmentTask : public Task {
  public:
    CaptureEnvironmentTask() : Task("Capture environment") {}

    void run(TracerState& state) override {
        capture_user(state);
        capture_timezone(state);
    }

  private:
    static void capture_user(TracerState& state) {
        state.manifest.cwd = std::filesystem::current_path();
        LOG(DEBUG) << "Current working directory: " << state.manifest.cwd;
        state.manifest.user = UserInfo::get_current_user_info();
        LOG(DEBUG) << "Current user: " << state.manifest.user.username;

        if (environ != nullptr) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            for (char** env = environ; *env != nullptr; ++env) {
                std::string s(*env);
                size_t pos = s.find('=');
                if (pos != std::string::npos) {
                    state.manifest.envir.emplace(s.substr(0, pos),
                                                 s.substr(pos + 1));
                } else {
                    LOG(WARN) << "Invalid environment variable: '" << s << "'";
                }
            }
        } else {
            LOG(WARN) << "Failed to get environment variables";
        }
    }

    static void capture_timezone(TracerState& state) {
        if (auto tz = get_system_timezone(); tz) {
            state.manifest.timezone = *tz;
        } else {
            LOG(WARN) << "Failed to get timezone information, fallback to "
                      << kDefaultTimezone;
            state.manifest.timezone = kDefaultTimezone;
        }
    }
};

class Tracer {
  public:
    explicit Tracer(Options options) : options_{std::move(options)} {}

    void execute() {
        configure();
        run_pipeline();
    }

    void stop() const {
        if (current_task_ != nullptr) {
            current_task_->stop();
        }
    }

  private:
    void run_pipeline() {
        std::vector<std::unique_ptr<Task>> tasks;

        tasks.push_back(std::make_unique<CaptureEnvironmentTask>());

        tasks.push_back(
            std::make_unique<FileTracingTask>(options_.ignore_file_list));

        tasks.push_back(std::make_unique<ResolveFileTask>(
            options_.R_bin, options_.ignore_file_list));

        tasks.push_back(std::make_unique<ResolveRPackageSystemDependencies>());

        if (!options_.skip_manifest) {
            tasks.push_back(std::make_unique<EditManifestTask>());
        }

        tasks.push_back(std::make_unique<DockerFileBuilderTask>(
            options_.output_dir, options_.docker_base_image,
            options_.docker_sudo_access));

        tasks.push_back(std::make_unique<MakefileBuilderTask>(
            options_.makefile, options_.docker_image_tag,
            options_.docker_container_name));

        if (options_.run_make) {
            tasks.push_back(
                std::make_unique<RunMakefileTask>(options_.makefile));
        }

        TracerState state{
            .dpkg_database = DpkgDatabase::system_database(),
            .rpkg_database = RpkgDatabase::from_R(options_.R_bin),
            .traced_files = {},
            .manifest = {},
        };

        // initialize from options
        state.manifest.cmd = options_.cmd;
        for (auto& f : options_.results) {
            state.manifest.copy_files.emplace(f, FileStatus::Result);
        }

        for (auto& task : tasks) {
            run(*task, state);
        }
    }

    void configure() {
        Logger::get().set_max_level(options_.log_level);

        fs::create_directory(options_.output_dir);

        if (options_.makefile.empty()) {
            options_.makefile = options_.output_dir / "Makefile";
        }
    }

    void run(Task& task, TracerState& state) {
        current_task_ = &task;

        LOG(INFO) << task.name() << " starting";

        auto elapsed = stopwatch([&] { task.run(state); });

        current_task_ = nullptr;

        LOG(INFO) << task.name() << " finished in "
                  << format_elapsed_time(elapsed);
    }

    Options options_;
    Task* current_task_{};
};

#endif // TRACER_H
