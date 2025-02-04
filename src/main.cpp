#include "cli.h"
#include "common.h"
#include "dockerfile.h"
#include "file_tracer.h"
#include "fs.h"
#include "logger.h"
#include "manifest.h"
#include "process.h"
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

    // retrieve the passwd struct for the user
    passwd* pwd = getpwuid(uid);
    if (!pwd) {
        throw std::runtime_error("Failed to get passwd struct for UID " +
                                 std::to_string(uid));
    }

    std::string username = pwd->pw_name;
    std::string home_directory = pwd->pw_dir;
    std::string shell = pwd->pw_shell;

    // primary group information
    group* grp = getgrgid(gid);
    if (!grp) {
        throw std::runtime_error("Failed to get group struct for GID " +
                                 std::to_string(gid));
    }
    GroupInfo primary_group = {gid, grp->gr_name};

    // get the list of groups
    int n_groups = 0;
    getgrouplist(username.c_str(), gid, nullptr,
                 &n_groups); // Get number of groups

    std::vector<gid_t> group_ids(n_groups);
    if (getgrouplist(username.c_str(), gid, group_ids.data(), &n_groups) ==
        -1) {
        throw std::runtime_error("Failed to get group list for user " +
                                 username);
    }

    // map gids to GroupInfo
    std::vector<GroupInfo> groups;
    for (gid_t group_id : group_ids) {
        group* g = getgrgid(group_id);
        if (g) {
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

  public:
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
        if (monitor_) {
            monitor_->stop();
        }
    }

  private:
    std::vector<std::string> const& cmd_;
    SyscallMonitor* monitor_{};
};

struct Options {
    fs::path R_bin{"R"};
    std::vector<std::string> cmd;
    std::string docker_base_image{"ubuntu:22.04"};
    std::string docker_image_tag{"r4r/test"};
    fs::path output_dir{"."};
};

Options parse_cmd_args(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        args.emplace_back(argv[i]);
    }
    return {.cmd = args};
}

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
                for (auto& [k, v] : trace.vars) {
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

class ManifestTask : public Task<Manifest> {
  public:
    explicit ManifestTask(Options const& options, fs::path const& cwd,
                          std::vector<FileInfo>& files)
        : Task{"manifest"}, options_{options}, cwd_{cwd}, files_{files} {}

  private:
    Manifest run(Logger& log, [[maybe_unused]] std::ostream& ostream) override {

        Manifest manifest;
        manifest.add<IgnoreFilesManifest>("ignore");
        manifest.add<DebPackagesManifest>("deb");
        manifest.add<CRANPackagesManifest>("cran", options_.R_bin,
                                           options_.output_dir);
        manifest.add<CopyFileManifest>("copy", cwd_, options_.output_dir);

        LOG_INFO(log) << "Resolving " << files_.size() << " files";
        manifest.load_from_files(files_);

        ManifestFormat manifest_content;

        manifest_content.preamble(
            "This is the manifest file generated by R4R.\n"
            "You can update its content by either adding or "
            "removing/commenting lines in the corresponding sections.");

        manifest.write_to_manifest(manifest_content);

        auto manifest_file = TempFile{"r4r-manifest", ".conf"};
        LOG_DEBUG(log) << "Writing manifest to: " << *manifest_file;
        write_to_file(*manifest_file, manifest_content);
        auto ts = fs::last_write_time(*manifest_file);

        if (open_manifest(*manifest_file) &&
            fs::last_write_time(*manifest_file) != ts) {

            LOG_DEBUG(log) << "Rereading manifest from: " << *manifest_file;
            std::ifstream stream{*manifest_file};
            manifest.load_from_manifest(stream);
        }

        return manifest;
    }

  private:
    static std::string read_manifest(fs::path const& path) {
        std::string input = read_from_file(path);
        std::istringstream iss(input);
        std::ostringstream oss;
        std::string line;
        bool first = true;

        while (std::getline(iss, line)) {
            line = string_trim(line);
            if (line.empty() || line.starts_with('#')) {
                continue;
            }

            if (!first) {
                oss << '\n';
            }
            oss << line;
            first = false;
        }

        return oss.str();
    }

    bool open_manifest(fs::path const& path) {
        char const* editor = std::getenv("VISUAL");
        if (!editor) {
            editor = std::getenv("EDITOR");
        }
        if (!editor) {
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
    Options const& options_;
    fs::path const& cwd_;
    std::vector<FileInfo>& files_;
};

class DockerFileBuilderTask : public Task<DockerFile> {
  public:
    DockerFileBuilderTask(Options const& options, Environment const& envir,
                          Manifest const& manifest)
        : Task{"dockerfile-builder"}, options_{options}, envir_{envir},
          manifest_{manifest} {}

    DockerFile run([[maybe_unused]] Logger& log,
                   [[maybe_unused]] std::ostream& ostream) override {

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

        return builder.build();
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
                         "apt-get install -y --no-install-recommend locales",
                         "locale-gen $LANG", "update-locale LANG=$LANG)"});
        }
    }

    void create_user(DockerFileBuilder& builder) {
        std::vector<std::string> cmds;
        auto& user = envir_.user;

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
        auto& env = envir_.vars;
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
        builder.run(STR("mkdir -p " << envir_.cwd));
        builder.workdir(envir_.cwd);
        builder.user(envir_.user.username);

        std::vector<std::string> args;
        for (auto const& arg : options_.cmd) {
            args.push_back(escape_cmd_arg(arg));
        }

        builder.cmd(string_join(args, ' '));
    }

    Options const& options_;
    Environment const& envir_;
    Manifest const& manifest_;
};

class DockerImage {};

class DockerImageBuilder : public Task<DockerImage> {
  public:
    DockerImageBuilder(Options const& options, DockerFile const& docker_file)
        : Task{"docker-image-builder"}, options_{options},
          docker_file_{docker_file} {}

    DockerImage run([[maybe_unused]] Logger& log,
                    [[maybe_unused]] std::ostream& ostream) override {
        (void)options_;

        docker_file_.save();
        auto process = Command("docker")
                           .arg("build")
                           .arg("--rm")
                           .arg("-t")
                           .arg(options_.docker_image_tag)
                           .arg(".")
                           .current_dir(docker_file_.context_dir())
                           .spawn();

        if (process.wait() != 0) {
            throw TaskException("Failed to build the Docker image");
        }

        return DockerImage{};
    }

  private:
    Options const& options_;
    DockerFile const& docker_file_;
};

class Tracer {
  public:
    explicit Tracer(Options options)
        : options_{std::move(options)}, runner_{std::cout} {}

    void trace() {
        auto envir = runner_.run(CaptureEnvironmentTask{});
        auto files = runner_.run(TracingTask{options_.cmd});
        auto manifest = runner_.run(ManifestTask{options_, envir.cwd, files});
        auto docker_file =
            runner_.run(DockerFileBuilderTask{options_, envir, manifest});
        auto docker_image =
            runner_.run(DockerImageBuilder{options_, docker_file});

        (void)docker_image;

        // rerun();
        // diff();
    }

    void stop() { runner_.stop(); }

  private:
    void trace_program() {}

    static inline Logger& log_ = LogManager::logger("tracer");
    Options options_;
    TaskRunner runner_;
};

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
        tracer.trace();
        return 0;
    } catch (TaskException& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
