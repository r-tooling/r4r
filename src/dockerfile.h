#ifndef DOCKERFILE_H
#define DOCKERFILE_H

#include "common.h"
#include "util.h"
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

struct DockerFile {
    fs::path context_dir;
    std::string dockerfile;
    std::vector<fs::path> copied_files;
};

class DockerFileBuilder {
  public:
    explicit DockerFileBuilder(std::string base_image, fs::path context_dir)
        : base_image_{std::move(base_image)}, context_dir_{context_dir} {}

    DockerFileBuilder& run(std::string const& command) {
        commands_.emplace_back("RUN " + command);
        return *this;
    }

    DockerFileBuilder& cmd(std::string const& command) {
        commands_.emplace_back("CMD " + command);
        return *this;
    }

    DockerFileBuilder& env(std::string const& key, std::string const& value) {
        commands_.emplace_back("ENV " + key + "=" + value);
        return *this;
    }

    DockerFileBuilder& add(std::string const& src, std::string const& dest) {
        commands_.emplace_back("ADD " + src + " " + dest);
        return *this;
    }

    DockerFileBuilder& copy(std::vector<fs::path> const& srcs,
                            std::string const& dest) {
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

    DockerFileBuilder& entrypoint(std::string const& command) {
        commands_.emplace_back("ENTRYPOINT " + command);
        return *this;
    }

    DockerFileBuilder& user(std::string const& user) {
        commands_.emplace_back("USER " + user);
        return *this;
    }

    DockerFileBuilder& workdir(std::string const& path) {
        commands_.emplace_back("WORKDIR " + path);
        return *this;
    }

    DockerFileBuilder& comment(std::string const& text) {
        commands_.emplace_back("# " + text);
        return *this;
    }

    DockerFileBuilder& nl() {
        commands_.emplace_back("");
        return *this;
    }

    DockerFile build() const {
        std::ostringstream dockerfile;
        dockerfile << "FROM " << base_image_ << "\n";
        for (auto& cmd : commands_) {
            dockerfile << cmd << "\n";
        }

        return DockerFile{context_dir_, dockerfile.str(), copied_files_};
    }

  private:
    std::string base_image_;
    fs::path context_dir_;
    std::vector<std::string> commands_;
    std::vector<fs::path> copied_files_;
};

#endif // DOCKERFILE_H
