#ifndef DOCKERFILE_BUILDER_H
#define DOCKERFILE_BUILDER_H

#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <sstream>
#include <utility>

class DockerFileBuilder {
  public:
    explicit DockerFileBuilder(std::string base_image)
        : base_image_(std::move(base_image)) {}

    DockerFileBuilder& run(const std::string& command) {
        commands_.emplace_back("RUN " + command);
        return *this;
    }

    DockerFileBuilder& cmd(const std::string& command) {
        commands_.emplace_back("CMD " + command);
        return *this;
    }

    DockerFileBuilder& expose(int port) {
        commands_.emplace_back("EXPOSE " + std::to_string(port));
        return *this;
    }

    DockerFileBuilder& env(const std::string& key, const std::string& value) {
        commands_.emplace_back("ENV " + key + "=" + value);
        return *this;
    }

    DockerFileBuilder& add(const std::string& src, const std::string& dest) {
        commands_.emplace_back("ADD " + src + " " + dest);
        return *this;
    }

    DockerFileBuilder& copy(const std::string& src, const std::string& dest) {
        commands_.emplace_back("COPY " + src + " " + dest);
        return *this;
    }

    DockerFileBuilder& entrypoint(const std::string& command) {
        commands_.emplace_back("ENTRYPOINT " + command);
        return *this;
    }

    DockerFileBuilder& volume(const std::string& path) {
        commands_.emplace_back("VOLUME " + path);
        return *this;
    }

    DockerFileBuilder& user(const std::string& user) {
        commands_.emplace_back("USER " + user);
        return *this;
    }

    DockerFileBuilder& workdir(const std::string& path) {
        commands_.emplace_back("WORKDIR " + path);
        return *this;
    }

    DockerFileBuilder& comment(const std::string& text) {
        commands_.emplace_back("# " + text);
        return *this;
    }

    DockerFileBuilder& nl() {
        commands_.emplace_back("");
        return *this;
    }

    // Output the Dockerfile to an ostream
    void build(std::ostream& os) const {
        os << "FROM " << base_image_ << "\n\n";
        for (const auto& command : commands_) {
            os << command << "\n";
        }
    }

  private:
    std::string base_image_;
    std::vector<std::string> commands_;
};

#endif // DOCKERFILE_BUILDER_H