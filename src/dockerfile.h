#ifndef DOCKERFILE_H
#define DOCKERFILE_H

#include "common.h"
#include "util_fs.h"
#include "util.h"
#include <sstream>
#include <string>
#include <utility>
#include <vector>

class DockerFile {
public:
    DockerFile(fs::path context_dir, std::string dockerfile,
               std::vector<fs::path> copied_files)
        : context_dir_{std::move(context_dir)},
          dockerfile_{std::move(dockerfile)},
          copied_files_{std::move(copied_files)} {
    }

    [[nodiscard]] std::string const& dockerfile() const { return dockerfile_; }

    [[nodiscard]] std::vector<fs::path> const& copied_files() const {
        return copied_files_;
    }

    [[nodiscard]] fs::path const& context_dir() const { return context_dir_; }

    void save(fs::path const& path) const {
        std::ofstream file{path};
        file << dockerfile_;
    }

    void save() const { save(context_dir_ / "Dockerfile"); }

private:
    fs::path context_dir_;
    std::string dockerfile_;
    std::vector<fs::path> copied_files_;
};

class DockerFileBuilder {
public:
    explicit DockerFileBuilder(std::string base_image, fs::path context_dir)
        : base_image_{std::move(base_image)},
          context_dir_{std::move(context_dir)} {
    }

    DockerFileBuilder& run(std::string const& command);
    DockerFileBuilder& run(std::vector<std::string> const& commands);
    DockerFileBuilder& cmd(std::vector<std::string> const& commands);
    DockerFileBuilder& env(std::string const& key, std::string const& value);
    DockerFileBuilder& env(std::vector<std::string> const& envs);
    DockerFileBuilder& add(std::string const& src, std::string const& dest);
    DockerFileBuilder& copy(std::vector<fs::path> const& srcs,
                            std::string const& dest);
    DockerFileBuilder& entrypoint(std::string const& command);
    DockerFileBuilder& user(std::string const& user);
    DockerFileBuilder& workdir(std::string const& path);
    DockerFileBuilder& comment(std::string const& text);
    DockerFileBuilder& nl();

    [[nodiscard]] DockerFile build() const;

private:
    std::string base_image_;
    fs::path context_dir_;
    std::vector<std::string> commands_;
    std::vector<fs::path> copied_files_;
};

inline DockerFileBuilder& DockerFileBuilder::run(
    std::vector<std::string> const& commands) {
    std::string cmds = string_join(commands, " && \\\n  ");
    commands_.emplace_back("RUN " + cmds);
    return *this;
}

inline DockerFileBuilder& DockerFileBuilder::cmd(
    std::vector<std::string> const& commands) {
    std::string cmd;
    for (size_t i = 0; auto const& c : commands) {
        cmd += escape_cmd_arg(c, false, true);
        if (++i < commands.size()) {
            cmd += ", ";
        }
    }

    commands_.emplace_back("CMD [" + cmd + "]");
    return *this;
}

inline DockerFileBuilder& DockerFileBuilder::env(std::string const& key,
                                                 std::string const& value) {
    commands_.emplace_back("ENV " + key + "=" + value);
    return *this;
}

inline DockerFileBuilder& DockerFileBuilder::env(
    std::vector<std::string> const& envs) {
    std::string cmd = string_join(envs, " \\\n  ");
    commands_.emplace_back("ENV " + cmd);
    return *this;
}

inline DockerFileBuilder& DockerFileBuilder::add(std::string const& src,
                                                 std::string const& dest) {
    commands_.emplace_back("ADD " + src + " " + dest);
    return *this;
}

inline DockerFileBuilder& DockerFileBuilder::copy(
    std::vector<fs::path> const& srcs, std::string const& dest) {
    std::vector<std::string> names;
    names.reserve(srcs.size());

    for (auto const& src : srcs) {
        if (!is_sub_path(src, context_dir_)) {
            throw std::runtime_error(
                STR("Source path "
                    << src << " is not a subpath of context directory "
                    << context_dir_));
        }
        names.push_back(fs::relative(src, context_dir_).string());
        copied_files_.push_back(src);
    }

    commands_.emplace_back(
        STR("COPY " << string_join(names, ' ') << " " << dest));

    return *this;
}

inline DockerFileBuilder& DockerFileBuilder::entrypoint(
    std::string const& command) {
    commands_.emplace_back("ENTRYPOINT " + command);
    return *this;
}

inline DockerFileBuilder& DockerFileBuilder::user(std::string const& user) {
    commands_.emplace_back("USER " + user);
    return *this;
}

inline DockerFileBuilder& DockerFileBuilder::workdir(std::string const& path) {
    commands_.emplace_back("WORKDIR " + path);
    return *this;
}

inline DockerFileBuilder& DockerFileBuilder::comment(std::string const& text) {
    commands_.emplace_back("# " + text);
    return *this;
}

inline DockerFileBuilder& DockerFileBuilder::nl() {
    commands_.emplace_back("");
    return *this;
}

inline DockerFile DockerFileBuilder::build() const {
    std::ostringstream dockerfile;
    dockerfile << "FROM " << base_image_ << "\n";
    for (auto const& cmd : commands_) {
        dockerfile << cmd << "\n\n";
    }

    return DockerFile{context_dir_, dockerfile.str(), copied_files_};
}

#endif // DOCKERFILE_H
