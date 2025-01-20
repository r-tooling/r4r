#pragma once

#include "common.hpp"
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace util {

bool is_sub_path(fs::path const& path, fs::path const& base);

std::string execute_command(std::string const& command);

std::string escape_env_var_definition(std::string env);

std::string escape_cmd_arg(std::string const& arg);

template <typename T, typename S>
void print_collection(std::ostream& os, T const& collection, S const& sep) {
    if (std::empty(collection)) {
        return;
    }

    auto it = std::begin(collection);
    auto end = std::end(collection);

    // print the first element without a separator
    os << *it++;
    for (; it != end; ++it) {
        os << sep << *it;
    }
}

template <typename T, typename S>
std::string mk_string(T const& collection, S const& sep) {
    std::ostringstream res;
    print_collection(res, collection, sep);
    return res.str();
}

template <typename FileCollection>
void create_tar_archive(fs::path const& archive, FileCollection const& files) {
    FILE* temp_file = std::tmpfile();
    if (!temp_file) {
        throw std::runtime_error("Error creating temporary file.");
    }

    for (auto const& file : files) {
        std::fprintf(temp_file, "%s\n", file.string().c_str());
    }

    std::fflush(temp_file);
    std::string command = "tar --absolute-names --preserve-permissions -cvf " +
                          archive.string() + " --files-from=/dev/fd/" +
                          std::to_string(fileno(temp_file));

    try {
        util::execute_command(command);
    } catch (std::exception const& e) {
        std::fclose(temp_file);
        std::string msg =
            "Error creating tar archive: " + archive.string() + ": " + e.what();
        throw std::runtime_error(msg);
    }
}

template <typename T>
class FilesystemTrie {
  private:
    struct Node {
        std::unordered_map<std::string, std::shared_ptr<Node>> children;
        T value;

        explicit Node(T const& value) : value(value){};
    };

    std::shared_ptr<Node> root;
    T default_value;

  public:
    explicit FilesystemTrie(T const& default_value)
        : root(std::make_shared<Node>(default_value)),
          default_value(default_value) {}

    void insert(fs::path const& path, T const& value) {
        auto node = root;

        for (auto& path_part : path) {
            auto part = path_part.string();
            if (part.empty()) {
                continue;
            }

            if (!node->children.count(part)) {
                node->children[part] = std::make_shared<Node>(default_value);
            }

            node = node->children[part];
        }

        // FIXME: Handle the case when this is already set
        // ideally there should be some closure which
        // would handle it? checking if it is a directory or
        // a file? reporting an error if it is a file?
        node->value = value;
    }

    [[nodiscard]] T const* find(fs::path const& path) const {
        auto node = root;

        for (auto& path_part : path) {
            auto part = path_part.string();

            if (part.empty()) {
                continue;
            }
            if (!node->children.count(part)) {
                return {};
            }
            node = node->children[part];
        }

        return &node->value;
    }

    [[nodiscard]] T const& find_last_matching(fs::path const& path) const {
        auto node = root;

        for (const auto& it : path) {
            auto part = it.string();

            if (part.empty()) {
                continue;
            }

            if (!node->children.contains(part)) {
                return node->value;
            }

            node = node->children[part];
        }

        return node->value;
    }

    bool is_empty() { return root->children.empty(); }
};

} // namespace util
