#include "argparser.h"
#include "cli.h"
#include "common.h"
#include "config.h"
#include "dockerfile.h"
#include "dpkg_database.h"
#include "file_tracer.h"
#include "fs.h"
#include "logger.h"
#include "manifest.h"
#include "process.h"
#include "resolvers.h"
#include "rpkg_database.h"
#include "syscall_monitor.h"
#include "util.h"

#include <csignal>
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

#include <cerrno>

#include <unordered_map>
#include <utility>
#include <vector>

std::function<void(int)> global_signal_handler;

void register_signal_handlers(std::function<void(int)> handler) {
    global_signal_handler = std::move(handler);
    std::array signals = {SIGINT, SIGTERM};
    for (int sig : signals) {
        auto status = signal(sig, [](int sig) { global_signal_handler(sig); });
        if (status == SIG_ERR) {
            throw make_system_error(errno,
                                    STR("Failed to register signal "
                                        << strsignal(sig) << " handler"));
        }
    }
}

struct GroupInfo {
    gid_t gid;
    std::string name;

    friend std::ostream& operator<<(std::ostream& os, GroupInfo const& group) {
        os << "GroupInfo {\n";
        prefixed_ostream(os, "  ", [&] {
            os << "gid: " << group.gid << "\n";
            os << "name: '" << group.name << "\n";
        });
        os << "}";
        return os;
    }
};

struct UserInfo {
    uid_t uid;
    GroupInfo group;

    std::string username;
    std::string home_directory;
    std::string shell;
    std::vector<GroupInfo> groups;

    friend std::ostream& operator<<(std::ostream& os, UserInfo const& user) {
        os << "UserInfo {\n";
        prefixed_ostream(os, "  ", [&] {
            os << "uid: " << user.uid << "\n";
            os << "group: " << user.group << "\n";
            os << "username: '" << user.username << "'\n";
            os << "home_directory: '" << user.home_directory << "'\n";
            os << "shell: '" << user.shell << "'\n";
            os << "groups:\n";
            if (!user.groups.empty()) {
                prefixed_ostream(os, "  ", [&] {
                    for (auto const& g : user.groups) {
                        os << "- " << g << "\n";
                    }
                });
            }
            os << "\n";
        });
        os << "}";
        return os;
    }
};

UserInfo get_user_info() {
    uid_t uid = getuid();
    gid_t gid = getgid();

    passwd* pwd = getpwuid(uid);
    if (pwd == nullptr) {
        throw std::runtime_error("Failed to get passwd struct for UID " +
                                 std::to_string(uid));
    }

    std::string username = pwd->pw_name;
    std::string home_directory = pwd->pw_dir;
    std::string shell = pwd->pw_shell;

    // primary group information
    group* grp = getgrgid(gid);
    if (grp == nullptr) {
        throw std::runtime_error("Failed to get group struct for GID " +
                                 std::to_string(gid));
    }
    GroupInfo primary_group = {gid, grp->gr_name};

    // groups
    int n_groups = 0;
    getgrouplist(username.c_str(), gid, nullptr,
                 &n_groups); // Get number of groups

    std::vector<gid_t> group_ids(n_groups);
    if (getgrouplist(username.c_str(), gid, group_ids.data(), &n_groups) ==
        -1) {
        throw std::runtime_error("Failed to get group list for user " +
                                 username);
    }

    // get gids
    std::vector<GroupInfo> groups;
    for (gid_t group_id : group_ids) {
        group* g = getgrgid(group_id);
        if (g != nullptr) {
            groups.push_back({group_id, g->gr_name});
        }
    }

    return {uid, primary_group, username, home_directory, shell, groups};
}

class TaskException : public std::runtime_error {
  public:
    explicit TaskException(std::string const& message)
        : std::runtime_error{message} {}
};

class TracingTask : public Task<std::vector<FileInfo>> {
  public:
    explicit TracingTask(std::vector<std::string> const& cmd)
        : Task{"trace"}, cmd_(cmd) {}

    std::vector<FileInfo> run(Logger& log, std::ostream& output) override {
        LOG_INFO(log) << "Tracing program: " << string_join(cmd_, ' ');

        FileTracer tracer;
        SyscallMonitor monitor{cmd_, tracer};
        monitor.redirect_stdout(output);
        monitor.redirect_stderr(output);

        // this is just to support the stop()
        monitor_ = &monitor;

        auto result = monitor_->start();

        monitor_ = nullptr;

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
        if (monitor_ != nullptr) {
            monitor_->stop();
        }
    }

  private:
    std::vector<std::string> const& cmd_;
    SyscallMonitor* monitor_{};
};

struct Options {
    LogLevel log_level = LogLevel::Info;
    fs::path R_bin{"R"};
    std::vector<std::string> cmd;
    std::string docker_base_image{"ubuntu:22.04"};
    std::string docker_image_tag{STR(kBinaryName << "/test")};
    std::string docker_container_name{STR(kBinaryName << "-test")};
    fs::path output_dir{"."};
    fs::path dockerfile;
    fs::path makefile;
    AbsolutePathSet results;
};

struct Environment {
    fs::path cwd;
    std::unordered_map<std::string, std::string> vars;
    UserInfo user;

    friend std::ostream& operator<<(std::ostream& os,
                                    Environment const& trace) {
        os << "Environment {\n";
        prefixed_ostream(os, "  ", [&] {
            os << "'\n";
            os << "cwd: " << trace.cwd << "\n";
            os << "env:\n";
            prefixed_ostream(os, "  ", [&] {
                for (const auto& [k, v] : trace.vars) {
                    os << "- " << k << ": " << remove_ansi(v) << "\n";
                }
            });
            os << "user: ";
            prefixed_ostream(os, "  ", [&] { os << trace.user; });
            os << "\n";
        });

        os << "}";
        return os;
    }
};

class CaptureEnvironmentTask : public Task<Environment> {
  public:
    CaptureEnvironmentTask() : Task{"capture-environment"} {}

    Environment run(Logger& log,
                    [[maybe_unused]] std::ostream& ostream) override {
        Environment envir{};

        envir.cwd = std::filesystem::current_path();
        envir.user = get_user_info();

        if (environ != nullptr) {
            for (char** env = environ; *env != nullptr; ++env) {
                std::string s(*env);
                size_t pos = s.find('=');
                if (pos != std::string::npos) {
                    envir.vars.emplace(s.substr(0, pos), s.substr(pos + 1));
                } else {
                    LOG_WARN(log)
                        << "Invalid environment variable: '" << s << "'";
                }
            }
        } else {
            LOG_WARN(log) << "Unable to get environment variables";
        }

        return envir;
    }
};

class ResolveTask : public Task<Resolvers> {
  public:
    explicit ResolveTask(std::vector<FileInfo>& files, fs::path cwd,
                         fs::path R_bin)
        : Task{"resolve"}, files_{files}, cwd_{std::move(cwd)},
          R_bin_{std::move(R_bin)} {}

    Resolvers run(Logger& log,
                  [[maybe_unused]] std::ostream& ostream) override {

        auto dpkg_database =
            std::make_shared<DpkgDatabase>(DpkgDatabase::system_database());
        auto rpkg_database =
            std::make_shared<RpkgDatabase>(RpkgDatabase::from_R(R_bin_));

        Resolvers resolvers;
        resolvers.add<IgnoreFileResolver>("ignore");
        resolvers.add<DebPackageResolver>("deb", dpkg_database);
        resolvers.add<CRANPackageResolver>("cran", rpkg_database);
        resolvers.add<CopyFileResolver>("copy", cwd_);

        LOG_INFO(log) << "Resolving " << files_.size() << " files";
        resolvers.load_from_files(files_);

        return resolvers;
    }

  private:
    std::vector<FileInfo>& files_;
    fs::path cwd_;
    fs::path R_bin_;
};

class ManifestTask : public Task<Manifest> {
  public:
    explicit ManifestTask(Resolvers const& resolvers,
                          fs::path const& output_dir,
                          AbsolutePathSet& result_files)
        : Task{"manifest"}, resolvers_{resolvers}, output_dir_{output_dir},
          result_files_(result_files) {}

    Manifest run([[maybe_unused]] Logger& log,
                 [[maybe_unused]] std::ostream& ostream) override {
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
            LOG_DEBUG(log_) << "Saving manifest to: " << *manifest_file;
            std::ofstream stream{*manifest_file};
            save_manifest(manifest, stream);
        }

        auto ts = fs::last_write_time(*manifest_file);

        if (open_manifest(*manifest_file) &&
            fs::last_write_time(*manifest_file) != ts) {
            LOG_DEBUG(log_) << "Rereading manifest from: " << *manifest_file;
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
                << "By default, they will be copied, unless explicitly ignored.\n"
                << "C - mark file to be copied into the image.\n"
                << "R - mark as additional result file.")};
        // clang-format on

        std::ostringstream content;
        for (auto const& [path, status] : files) {
            if (status == FileStatus::Copy) {
                content << "C " << path << "\n";
            } else {
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
                LOG_WARN(log_) << "Invalid line: " << line;
                continue;
            }

            line = line.substr(1);
            line = string_trim(line);
            if (line.starts_with('"')) {
                if (line.ends_with('"')) {
                    line = line.substr(1, line.size() - 2);
                } else {
                    LOG_WARN(log_) << "Invalid path: " << line;
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
            LOG_ERROR(log_) << "No editor found. Set VISUAL or EDITOR "
                               "environment variable.";
            return false;
        }

        std::string command = STR(editor << " " << path.string());

        LOG_DEBUG(log_) << "Opening the manifest file: " << command;
        int status = std::system(command.c_str());
        if (status == -1) {
            LOG_ERROR(log_) << "Failed to open the manifest file: " << command;
            return false;
        }

        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            int exit_code = WEXITSTATUS(status);
            LOG_DEBUG(log_)
                << "Editor: " << command << " exit code: " << exit_code;
            return false;
        }

        if (WIFSIGNALED(status)) {
            int signal = WTERMSIG(status);
            LOG_DEBUG(log_)
                << "Editor: " << command << " terminated by signal: " << signal;
            return false;
        }

        return true;
    }

    static inline Logger& log_ = LogManager::logger("manifest");
    Resolvers const& resolvers_;
    fs::path const& output_dir_;
    AbsolutePathSet result_files_;
};

class DockerFileBuilderTask : public Task<DockerFile> {
  public:
    DockerFileBuilderTask(Options const& options, Environment const& envir,
                          Manifest const& manifest)
        : Task{"dockerfile-builder"}, options_{options}, envir_{envir},
          manifest_{manifest} {}

    DockerFile run([[maybe_unused]] Logger& log,
                   [[maybe_unused]] std::ostream& ostream) override {

        LOG_INFO(log) << "Generating Dockerfile: " << options_.dockerfile;

        DockerFileBuilder builder{options_.docker_base_image,
                                  options_.output_dir};

        builder.env("DEBIAN_FRONTEND", "noninteractive");
        builder.nl();
        set_locale(builder);
        builder.nl();
        create_user(builder);

        manifest_.write_to_docker(builder);

        builder.nl();
        set_environment(builder);
        builder.nl();
        prepare_command(builder);

        DockerFile docker_file = builder.build();
        docker_file.save(options_.dockerfile);

        return docker_file;
    }

  private:
    void set_locale(DockerFileBuilder& builder) {
        std::optional<std::string> lang = "C"s;
        if (auto it = envir_.vars.find("LANG"); it != envir_.vars.end()) {
            lang = it->second;
        }

        if (lang) {
            builder.env("LANG", *lang);
            builder.nl();

            builder.run({"apt-get update -y",
                         "apt-get install -y --no-install-recommends locales",
                         "locale-gen $LANG", "update-locale LANG=$LANG"});
        }
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

        builder.run(cmds);
    }

    void set_environment(DockerFileBuilder& builder) {
        auto const& env = envir_.vars;
        if (env.empty()) {
            return;
        }

        // FIXME: add to the task
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

struct DockerImage {
    std::string tag;
};

// FIXME: return void
class MakefileBuilderTask : public Task<fs::path> {
  public:
    explicit MakefileBuilderTask(Options const& options)
        : Task{"makefile"}, options_{options} {}

    fs::path run(Logger& log, [[maybe_unused]] std::ostream& ostream) override {
        LOG_INFO(log) << "Generating makefile: " << options_.makefile;

        std::ofstream makefile{options_.makefile};
        generate_makefile(makefile);

        return options_.makefile;
    }

    void generate_makefile(std::ostream& makefile) {
        // TODO: MAKEFILE_DIR := $(dir $(realpath $(lastword
        // $(MAKEFILE_LIST))))
        // TODO: pretty print
        makefile << "IMAGE_TAG = " << options_.docker_image_tag << "\n"
                 << "CONTAINER_NAME = " << options_.docker_container_name
                 << "\n"
                 << "TARGET_DIR = " << (options_.output_dir / "out").string()
                 << "\n\n"

                 << "fg_blue  = \033[1;34m\n"
                 << "fg_reset = \033[0m"

                 << ".PHONY: all build run copy clean\n\n"

                 << "all: clean copy\n\n"

                 << "build:\n"
                 << "\tdocker build -t $(IMAGE_TAG) .\n\n"

                 << "run: build\n"
                 << "\t@echo '$(fg_blue)[running]$(fg_reset)'\n"
                 << "\tdocker run -t --name $(CONTAINER_NAME) $(IMAGE_TAG)\n\n"

                 << "copy: run\n"
                 << "\tmkdir -p $(TARGET_DIR)\n";

        for (auto const& file : options_.results) {
            makefile << "\tdocker cp -L $(CONTAINER_NAME):" << file.string()
                     << " $(TARGET_DIR)\n";
        }

        makefile << "\n"

                 << "clean:\n"
                 << "\t- docker rm $(CONTAINER_NAME)\n"
                 << "\t- docker rmi $(IMAGE_TAG)\n"
                 << "\trm -rf $(TARGET_DIR)\n\n";
    }

  private:
    // FIXME: cherry-pick options
    Options const& options_;
};

class RunMakefileTask : public Task<int> {
  public:
    explicit RunMakefileTask(Options const& options)
        : Task{"docker-image-builder"}, options_{options} {}

    int run(Logger& log, [[maybe_unused]] std::ostream& ostream) override {
        LOG_INFO(log) << "Running Makefile: " << options_.makefile;
        auto exit_code = Command("make")
                             .arg("-f")
                             .arg(options_.makefile.filename())
                             .current_dir(options_.makefile.parent_path())
                             .spawn()
                             .wait();

        if (exit_code != 0) {
            throw TaskException("Failed to run make");
        }

        return exit_code;
    }

  private:
    Options const& options_;
};

class Tracer {
  public:
    explicit Tracer(Options const& options)
        : options_{options}, runner_{std::cout} {}

    void execute() {
        configure();
        run_pipeline();
    }

    void stop() { runner_.stop(); }

  private:
    void run_pipeline() {
        auto envir = runner_.run(CaptureEnvironmentTask{});
        auto files = runner_.run(TracingTask{options_.cmd});
        auto resolvers =
            runner_.run(ResolveTask{files, envir.cwd, options_.R_bin});
        auto manifest = runner_.run(
            ManifestTask{resolvers, options_.output_dir, options_.results});

        runner_.run(DockerFileBuilderTask{options_, envir, manifest});
        runner_.run(MakefileBuilderTask{options_});
        runner_.run(RunMakefileTask{options_});
    }

    void configure() {
        Logger& log = LogManager::root_logger();
        for (LogLevel level = LogLevel::Error; level <= LogLevel::Trace;
             --level) {
            log.disable(level);
        }
        for (LogLevel level = LogLevel::Error; level <= options_.log_level;
             --level) {
            log.enable(level);
        }

        fs::create_directory(options_.output_dir);

        if (options_.dockerfile.empty()) {
            options_.dockerfile = options_.output_dir / "Dockerfile";
        }
        if (options_.makefile.empty()) {
            options_.makefile = options_.output_dir / "Makefile";
        }
    }

    static inline Logger& log_ = LogManager::logger("tracer");
    Options options_;
    TaskRunner runner_;
};

Options parse_cmd_args(int argc, char* argv[]) {
    Options opts;
    ArgumentParser parser{std::string(kBinaryName)};

    parser.add_option('v', "verbose")
        .with_help("Make the tool more talkative (allow multiple)")
        .with_callback([&](auto) { --opts.log_level; });
    parser.add_option("docker-image-tag")
        .with_help("The docker image tag")
        .with_default(opts.docker_image_tag)
        .with_argument("NAME")
        .with_callback([&](auto& arg) { opts.docker_image_tag = arg; });
    parser.add_option("docker-container-name")
        .with_help("The docker container name")
        .with_default(opts.docker_container_name)
        .with_argument("NAME")
        .with_callback([&](auto& arg) { opts.docker_container_name = arg; });
    parser.add_option("result")
        .with_help("Path to a result file")
        .with_argument("PATH")
        .with_callback([&](auto& arg) { opts.results.insert(arg); });
    parser.add_option("output")
        .with_help("Path for the output")
        .with_argument("PATH")
        .with_callback([&](auto& arg) { opts.output_dir = arg; });
    parser.add_option("help")
        .with_help("Print this message")
        .with_callback([&](auto) {
            std::cout << parser.help();
            exit(0);
        });
    parser.add_positional("command")
        .required()
        .multiple()
        .with_help("The program to trace")
        .with_callback([&](auto& arg) { opts.cmd.push_back(arg); });

    try {
        parser.parse(argc, argv);
        return opts;
    } catch (ArgumentParserException const& e) {
        std::cerr << kBinaryName << ": " << e.what() << std::endl;
        std::cerr << kBinaryName << ": "
                  << "try '" << kBinaryName << " --help' for more information"
                  << std::endl;

        exit(1);
    }
}

int main(int argc, char* argv[]) {
    Options options = parse_cmd_args(argc, argv);
    Tracer tracer{options};

    // Interrupt signals generated in the terminal are delivered to the
    // active process group, which here includes both parent and child. A
    // signal manually generated and sent to an individual process (perhaps
    // with kill) will be delivered only to that process, regardless of
    // whether it is the parent or child. That is why we need to register a
    // signal handler that will terminate the tracee when the tracer
    // gets killed.

    register_signal_handlers([&, got_sigint = false](int sig) mutable {
        switch (sig) {
        case SIGTERM:
            tracer.stop();
            exit(1);
        case SIGINT:
            if (got_sigint) {
                std::cerr << "SIGINT twice, exiting the tracer!";
                exit(1);
            } else {
                std::cerr << "SIGINT, stopping the current task...";
                tracer.stop();
                got_sigint = true;
            }
            break;
        default:
            UNREACHABLE();
        }
    });

    try {
        tracer.execute();
        return 0;
    } catch (TaskException& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
