#ifndef TRACER_H
#define TRACER_H

#include "common.h"
#include "config.h"
#include "dockerfile.h"
#include "dpkg_database.h"
#include "file_tracer.h"
#include "filesystem_trie.h"
#include "logger.h"
#include "manifest.h"
#include "process.h"
#include "resolvers.h"
#include "rpkg_database.h"
#include "syscall_monitor.h"
#include "user.h"
#include "util.h"
#include "util_fs.h"
#include "util_io.h"

#include <fcntl.h>
#include <filesystem>

#include <cstdlib>
#include <fstream>
#include <grp.h>
#include <iostream>
#include <pwd.h>
#include <sstream>
#include <stdexcept>
#include <string>

#include <sys/select.h>
#include <unordered_map>
#include <utility>
#include <vector>

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
    fs::path dockerfile;
    fs::path makefile;
    AbsolutePathSet results;
    bool docker_sudo_access{true};
    // TODO: make this mutable so more files could be added from command line
    FileSystemTrie<bool> ignore_file_list = kDefaultIgnoredFiles;
};

class TaskBase {
  public:
    virtual ~TaskBase() = default;
    virtual void stop() {}
};

template <typename T>
class Task : public TaskBase {
  public:
    virtual T run() = 0;
};

class TaskException : public std::runtime_error {
  public:
    explicit TaskException(std::string const& message)
        : std::runtime_error{message} {}
};

using TracingResult = std::vector<FileInfo>;

class TracingTask : public Task<TracingResult> {
  public:
    explicit TracingTask(std::vector<std::string> const& cmd) : cmd_(cmd) {}

    TracingResult run() override {
        LOG(INFO) << "Tracing program: " << string_join(cmd_, ' ');

        auto old_log_sink =
            Logger::get().set_sink(std::make_unique<StoreSink>());

        FileTracer tracer;
        SyscallMonitor monitor{cmd_, tracer};
        monitor.redirect_stdout(std::cout);
        monitor.redirect_stderr(std::cerr);

        // this is just to support the stop()
        monitor_ = &monitor;
        auto [result, elapsed] = stopwatch([&] { return monitor_->start(); });
        monitor_ = nullptr;

        LOG(INFO) << "Finished tracing in " << format_elapsed_time(elapsed);

        // print the postponed messages
        auto sink = Logger::get().set_sink(std::move(old_log_sink));
        auto const& events =
            dynamic_cast<StoreSink*>(sink.get())->get_messages();

        LOG(INFO) << "Traced " << tracer.syscalls_count() << " syscalls and "
                  << tracer.files().size() << " files";

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
            std::vector<FileInfo> files;
            files.reserve(file_map.size());
            for (auto const& [key, value] : file_map) {
                files.push_back(value);
            }

            std::sort(files.begin(), files.end(),
                      [](auto const& lhs, auto const& rhs) {
                          return lhs.path < rhs.path;
                      });

            return files;
        }

        UNREACHABLE();
    }

    void stop() override {
        if (monitor_) {
            monitor_->stop();
        }
    }

  private:
    std::vector<std::string> const& cmd_;
    SyscallMonitor* monitor_{};
};

struct Environment {
    fs::path cwd;
    std::unordered_map<std::string, std::string> vars;
    UserInfo user;
    std::string timezone;
};

class CaptureEnvironmentTask : public Task<Environment> {
  public:
    Environment run() override {
        Environment envir;

        envir.cwd = std::filesystem::current_path();
        LOG(DEBUG) << "Current working directory: " << envir.cwd;
        envir.user = UserInfo::get_current_user_info();
        LOG(DEBUG) << "Current user: " << envir.user.username;

        if (environ != nullptr) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            for (char** env = environ; *env != nullptr; ++env) {
                std::string s(*env);
                size_t pos = s.find('=');
                if (pos != std::string::npos) {
                    envir.vars.emplace(s.substr(0, pos), s.substr(pos + 1));
                } else {
                    LOG(WARN) << "Invalid environment variable: '" << s << "'";
                }
            }
        } else {
            LOG(WARN) << "Failed to get environment variables";
        }

        if (auto tz = CaptureEnvironmentTask::get_system_timezone(); tz) {
            envir.timezone = *tz;
        } else {
            LOG(WARN) << "Failed to get timezone information, fallback to "
                      << kDefaultTimezone;
            envir.timezone = kDefaultTimezone;
        }

        return envir;
    }

  private:
    static inline std::string_view const kDefaultTimezone{"UTC"};
    static std::optional<std::string> get_system_timezone() {
        // 1. try TZ environment
        char const* tz_env = std::getenv("TZ");
        if (tz_env) {
            return {tz_env};
        }

        // 2. try reading from /etc/timezone
        std::ifstream tz_file("/etc/timezone");
        if (tz_file) {
            std::string timezone;
            std::getline(tz_file, timezone);
            return timezone;
        }

        // 3. timedatectl
        auto out = Command("timedatectl")
                       .arg("show")
                       .arg("--property=Timezone")
                       .arg("--value")
                       .output();

        if (out.exit_code == 0) {
            return out.stdout_data;
        }

        return {};
    }
};

class ResolveTask : public Task<Resolvers> {
  public:
    ResolveTask(ResolveTask const&) = delete;
    ResolveTask& operator=(ResolveTask const&) = delete;
    ~ResolveTask() override = default;
    ResolveTask(ResolveTask&&) = delete;
    ResolveTask& operator=(ResolveTask&&) = delete;

    explicit ResolveTask(std::vector<FileInfo>& files,
                         AbsolutePathSet const& result_files, fs::path cwd,
                         fs::path R_bin,
                         FileSystemTrie<bool> const& ignore_file_list)
        : files_{files}, result_files_{result_files}, cwd_{std::move(cwd)},
          R_bin_{std::move(R_bin)}, ignore_file_list_{ignore_file_list} {}

    Resolvers run() override {

        auto dpkg_database =
            std::make_shared<DpkgDatabase>(DpkgDatabase::system_database());
        auto rpkg_database =
            std::make_shared<RpkgDatabase>(RpkgDatabase::from_R(R_bin_));

        Resolvers resolvers;
        resolvers.add<IgnoreFileResolver>("ignore", ignore_file_list_);
        resolvers.add<DebPackageResolver>("deb", dpkg_database);
        resolvers.add<CRANPackageResolver>("cran", rpkg_database,
                                           dpkg_database);
        resolvers.add<CopyFileResolver>("copy", cwd_, result_files_);

        resolvers.load_from_files(files_);

        return resolvers;
    }

  private:
    std::vector<FileInfo>& files_;
    AbsolutePathSet const& result_files_;
    fs::path cwd_;
    fs::path R_bin_;
    FileSystemTrie<bool> const& ignore_file_list_;
};

class ManifestTask : public Task<Manifest> {
  public:
    explicit ManifestTask(Resolvers const& resolvers,
                          fs::path const& output_dir,
                          AbsolutePathSet& result_files)
        : resolvers_{resolvers}, output_dir_{output_dir},
          result_files_(result_files) {}

    Manifest run() override {
        Manifest manifest{output_dir_};
        resolvers_.add_to_manifest(manifest);
        edit_manifest(manifest);
        return manifest;
    }

  private:
    void edit_manifest(Manifest& manifest) {
        // TODO: check if running an interactive terminal
        auto& files = manifest.files();
        if (files.empty()) {
            return;
        }

        auto manifest_file = TempFile{"r4r-manifest", ".conf"};
        {
            LOG(DEBUG) << "Saving manifest to: " << *manifest_file;
            std::ofstream stream{*manifest_file};
            save_manifest(manifest, stream);
        }

        auto ts = fs::last_write_time(*manifest_file);

        if (open_manifest(*manifest_file) &&
            fs::last_write_time(*manifest_file) != ts) {
            LOG(DEBUG) << "Rereading manifest from: " << *manifest_file;
            std::ifstream stream{*manifest_file};
            load_manifest(manifest, stream);
        }
    }

    static ManifestFormat::Section
    create_copy_section(Manifest::Files const& files) {
        std::string preamble{
            // clang-format off
            STR("The following "
                << files.size() << " files has not been resolved.\n"
                << "# - ignore\n"
                << "C - mark file to be copied into the image.\n"
                << "R - mark as additional result file.")};
        // clang-format on

        std::vector<std::pair<fs::path, FileStatus>> sorted_files;
        sorted_files.reserve(files.size());
        for (auto const& f : files) {
            sorted_files.emplace_back(f);
        }

        std::sort(sorted_files.begin(), sorted_files.end(),
                  [](auto const& lhs, auto const& rhs) {
                      return lhs.first < rhs.first;
                  });

        std::ostringstream content;
        for (auto const& [path, status] : sorted_files) {
            switch (status) {
            case FileStatus::Copy:
                content << "C " << path << "\n";
                break;
            case FileStatus::Result:
                content << "R " << path << "\n";
                break;
            case FileStatus::IgnoreNoLongerExist:
                // nothing we can do
                break;
            default:
                content << ManifestFormat::comment() << " " << path << " "
                        << ManifestFormat::comment() << " " << status << "\n";
            }
        }

        return {.name = "copy", .content = content.str(), .preamble = preamble};
    }

    static void load_copy_section(ManifestFormat::Section const& section,
                                  Manifest::Files& copy_files,
                                  AbsolutePathSet& result_files) {
        std::istringstream stream{section.content};
        std::string line;

        copy_files.clear();

        while (std::getline(stream, line)) {
            bool copy;

            if (line.starts_with("C")) {
                copy = true;
            } else if (line.starts_with("R")) {
                copy = false;
            } else {
                LOG(WARN) << "Invalid manifest line: " << line;
                continue;
            }

            line = line.substr(1);
            line = string_trim(line);
            if (line.starts_with('"')) {
                if (line.ends_with('"')) {
                    line = line.substr(1, line.size() - 2);
                } else {
                    LOG(WARN) << "Invalid path: " << line;
                    continue;
                }
            }

            if (copy) {
                copy_files.emplace(line, FileStatus::Copy);
            } else {
                result_files.insert(line);
            }
        }
    }

    void load_manifest(Manifest& manifest, std::istream& stream) {
        // TODO: add >> operator?
        auto format = ManifestFormat::from_stream(stream);
        auto* section = format.get_section("copy");
        if (section == nullptr) {
            return;
        }
        load_copy_section(*section, manifest.files(), result_files_);
    }

    static void save_manifest(Manifest& manifest, std::ostream& stream) {
        ManifestFormat format;
        format.preamble(
            "This is the manifest file generated by R4R.\n"
            "You can update its content by either adding or "
            "removing/commenting lines in the corresponding sections.");

        format.add_section(create_copy_section(manifest.files()));

        stream << format;
    }

    static bool open_manifest(fs::path const& path) {
        char const* editor = std::getenv("VISUAL");
        if (editor == nullptr) {
            editor = std::getenv("EDITOR");
        }
        if (editor == nullptr) {
            LOG(ERROR) << "No editor found. Set VISUAL or EDITOR "
                          "environment variable.";
            return false;
        }

        LOG(DEBUG) << "Opening the manifest file: " << path << " using "
                   << editor;
        auto exit_code = Command{editor}.arg(path).spawn().wait();
        if (exit_code == -1) {
            LOG(ERROR) << "Failed to open the manifest file. Exit code: "
                       << exit_code;
            return false;
        }

        return true;
    }

    Resolvers const& resolvers_;
    fs::path const& output_dir_;
    AbsolutePathSet result_files_;
};

class DockerFileBuilderTask : public Task<DockerFile> {
  public:
    DockerFileBuilderTask(Options const& options, Environment const& envir,
                          Manifest const& manifest)
        : options_{options}, envir_{envir}, manifest_{manifest} {}

    DockerFile run() override {

        LOG(INFO) << "Generating Dockerfile: " << options_.dockerfile;

        DockerFileBuilder builder{options_.docker_base_image,
                                  options_.output_dir};

        builder.env("DEBIAN_FRONTEND", "noninteractive");

        set_basics(builder);
        create_user(builder);

        manifest_.write_to_docker(builder);

        set_environment(builder);
        prepare_command(builder);

        DockerFile docker_file = builder.build();
        docker_file.save(options_.dockerfile);

        return docker_file;
    }

  private:
    void set_basics(DockerFileBuilder& builder) {
        std::string lang = "C"s;
        if (auto it = envir_.vars.find("LANG"); it != envir_.vars.end()) {
            lang = it->second;
        }
        std::string tz = envir_.timezone;
        if (auto it = envir_.vars.find("TZ"); it != envir_.vars.end()) {
            tz = it->second;
        }

        builder.env("LANG", lang);
        builder.env("TZ", tz);
        builder.run(
            {"apt-get update -y",
             "apt-get install -y --no-install-recommends locales tzdata",
             "echo $LANG >> /etc/locale.gen", "locale-gen $LANG",
             "update-locale LANG=$LANG"});
    }

    void create_user(DockerFileBuilder& builder) {
        std::vector<std::string> cmds;
        auto const& user = envir_.user;

        // create the primary group
        cmds.push_back(
            STR("groupadd -g " << user.group.gid << " " << user.group.name));

        // create groups
        for (auto const& group : user.groups) {
            cmds.push_back(STR("(groupadd -g " << group.gid << " " << group.name
                                               << " || groupmod -g "
                                               << group.gid << " " << group.name
                                               << ")"));
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
                           << " -d " << user.home_directory << " -s "
                           << user.shell << " " << user.username));

        // ensure home directory exists
        cmds.push_back(STR("mkdir -p " << user.home_directory));
        cmds.push_back(STR("chown " << user.username << ":" << user.group.name
                                    << " " << user.home_directory));

        // sudo?
        if (options_.docker_sudo_access) {
            cmds.emplace_back("apt-get install -y sudo");
            cmds.push_back(STR("echo '"
                               << user.username
                               << " ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/"
                               << user.username));
            cmds.push_back(STR("chmod 0440 /etc/sudoers.d/" << user.username));
        }

        builder.run(cmds);
    }

    void set_environment(DockerFileBuilder& builder) {
        auto const& env = envir_.vars;
        if (env.empty()) {
            return;
        }

        // FIXME: add to the environment task (possibly to manifest)
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
        sorted_env.reserve(env.size());

        for (auto const& [k, v] : env) {
            if (!ignored_env.contains(k)) {
                sorted_env.push_back(STR(k << "=\"" << v << "\""));
            }
        }

        std::sort(sorted_env.begin(), sorted_env.end());

        builder.env(sorted_env);
    }

    void prepare_command(DockerFileBuilder& builder) {
        builder.run(
            {STR("mkdir -p " << envir_.cwd),
             STR("chown " << envir_.user.username << ":"
                          << envir_.user.group.name << " " << envir_.cwd)});
        builder.workdir(envir_.cwd);
        builder.user(envir_.user.username);
        builder.cmd(options_.cmd);
    }

    Options const& options_;
    Environment const& envir_;
    Manifest const& manifest_;
};

class MakefileBuilderTask : public Task<fs::path> {
  public:
    explicit MakefileBuilderTask(Options const& options) : options_{options} {}

    fs::path run() override {
        LOG(INFO) << "Generating makefile: " << options_.makefile;

        std::ofstream makefile{options_.makefile};
        generate_makefile(makefile);

        return options_.makefile;
    }

    void generate_makefile(std::ostream& makefile) {
        // TODO: make sure it executes in the same dir as the makefile
        // MAKEFILE_DIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))

        makefile << "IMAGE_TAG = " << options_.docker_image_tag << "\n"
                 << "CONTAINER_NAME = " << options_.docker_container_name
                 << "\n"
                 // TODO: add to settings
                 << "TARGET_DIR = result"
                 << "\n\n"

                 << ".PHONY: all build run copy clean\n\n"

                 << "all: clean copy\n\n"

                 // clang-format off
                 << "build:\n"
                 << "\t@echo 'Building docker image $(IMAGE_TAG)'\n"
                 << "\t@docker build --progress=plain -t $(IMAGE_TAG) . 2>&1"
                 // << " | tee docker-build.log"
                 // << " | fold"
                 << "\n\n"
                 // clang-format on

                 // clang-format off
                 << "run: build\n"
                 << "\t@echo 'Running container $(CONTAINER_NAME)'\n"
                 << "\t@docker run -t --name $(CONTAINER_NAME) $(IMAGE_TAG) 2>&1"
                 // << " | tee docker-run.log"
                 // << " | fold"
                 << "\n\n"
                 // clang-format on

                 << "copy: run\n"
                 << "\t@echo\n" // add a new line in case the docker run did not
                                // finish with one
                 << "\t@echo 'Copying files'\n"
                 << "\t@mkdir -p $(TARGET_DIR)\n";

        for (auto const& file : options_.results) {
            makefile << "\t@echo -n '  - " << file << "...'\n"
                     << "\t@docker cp -L $(CONTAINER_NAME):" << file.string()
                     << " $(TARGET_DIR) 2>/dev/null && echo ' done' || echo ' "
                        "failed'"
                     << "\n";
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

  private:
    // FIXME: cherry-pick options
    Options const& options_;
};

class RunMakefileTask : public Task<int> {
  public:
    explicit RunMakefileTask(fs::path const& makefile) : makefile_{makefile} {}

    RunMakefileTask(RunMakefileTask const&) = delete;
    RunMakefileTask& operator=(RunMakefileTask const&) = delete;

    int run() override {
        LOG(INFO) << "Running Makefile: " << makefile_;
        int exit_code = run_makefile_target("all", "make> ");
        if (exit_code != 0) {
            throw TaskException("Failed to run make");
        }
        return exit_code;
    }

  private:
    int run_makefile_target(std::string const& target,
                            std::string const& prefix) {
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

    fs::path const& makefile_;
};

class Tracer {
  public:
    explicit Tracer(Options options) : options_{std::move(options)} {}

    void execute() {
        configure();
        run_pipeline();
    }

    void stop() {
        if (current_task_ != nullptr) {
            current_task_->stop();
        }
    }

  private:
    void run_pipeline() {
        auto envir = run("Capture environment", CaptureEnvironmentTask{});

        auto files = run("File tracer", TracingTask{options_.cmd});

        auto resolvers =
            run("File resolver",
                ResolveTask{files, options_.results, envir.cwd, options_.R_bin,
                            options_.ignore_file_list});
        auto manifest =
            run("Manifest builder",
                ManifestTask{resolvers, options_.output_dir, options_.results});

        run("Docker file builder",
            DockerFileBuilderTask{options_, envir, manifest});

        run("Makefile builder", MakefileBuilderTask{options_});

        run("Make runner", RunMakefileTask{options_.makefile});
    }

    void configure() {
        Logger::get().max_level(options_.log_level);

        fs::create_directory(options_.output_dir);

        if (options_.dockerfile.empty()) {
            options_.dockerfile = options_.output_dir / "Dockerfile";
        }
        if (options_.makefile.empty()) {
            options_.makefile = options_.output_dir / "Makefile";
        }
    }

    template <typename T>
    T run(std::string const& name, Task<T>&& task) {
        return run(name, task);
    }

    template <typename T>
    T run(std::string const& name, Task<T>& task) {
        current_task_ = &task;

        LOG(INFO) << name << " starting";

        auto [result, elapsed] = stopwatch([&] { return task.run(); });
        current_task_ = nullptr;
        LOG(INFO) << name << " finished in " << format_elapsed_time(elapsed);
        return result;
    }

    Options options_;
    TaskBase* current_task_{};
};

#endif // TRACER_H
