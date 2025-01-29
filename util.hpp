#pragma once

#include "common.hpp"
#include <cstdint>
#include <memory>
#include <random>
#include <ranges>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
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

std::vector<std::string> string_split(std::string const& str, char delim);

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

template <typename Collection>
std::unique_ptr<typename Collection::value_type[]>
collection_to_c_array(Collection const& container) {
    using T = typename Collection::value_type;
    static_assert(std::ranges::sized_range<Collection>);

    size_t const size = std::ranges::size(container);
    if (size == 0) {
        return nullptr;
    }

    std::unique_ptr<T[]> xs(new T[size + 1]); // +1 for NULL terminator
    std::ranges::copy(container, xs.get());
    xs[size] = T{};

    return xs;
}

template <typename Collection>
    requires std::is_same_v<typename Collection::value_type, std::string>
std::unique_ptr<char* const[]>
collection_to_c_array(Collection const& container) {
    size_t const size = std::ranges::size(container);
    if (size == 0) {
        return nullptr;
    }

    std::unique_ptr<char*[]> xs(new char*[size + 1]); // +1 for NULL terminator

    for (size_t i = 0; i < size; ++i) {
        xs[i] = const_cast<char*>(container[i].c_str());
    }
    xs[size] = nullptr;

    return xs;
}

bool is_executable(fs::path const& path);

std::string read_from_pipe(int pipe_fd);

struct WaitForSignalResult {
    enum Status { Success, Timeout, Exit, Signal } status;
    std::optional<int> detail;
};

WaitForSignalResult wait_for_signal(pid_t pid, int sig,
                                    std::chrono::milliseconds timeout);

struct Pipe {
    int read_fd;
    int write_fd;
};

Pipe create_pipe();

std::optional<fs::path> get_process_cwd(pid_t pid);

std::optional<fs::path> resolve_fd_filename(pid_t pid, int fd);

std::variant<std::uintmax_t, std::error_code> file_size(fs::path const& path);

std::string remove_ansi(std::string const& input);

template <typename Duration>
std::string format_elapsed_time(Duration elapsed) {
    using namespace std::chrono;
    using namespace std::chrono_literals;

    constexpr int MS_PER_SEC = 1000;
    constexpr int MS_PER_MIN = 60 * MS_PER_SEC;
    constexpr int MS_PER_HOUR = 60 * MS_PER_MIN;

    auto total_ms = duration_cast<milliseconds>(elapsed).count();

    if (total_ms < MS_PER_SEC) {
        return STR(total_ms << " ms");
    }

    auto total_seconds = duration_cast<duration<double>>(elapsed).count();
    if (total_ms < MS_PER_MIN) {
        return STR(std::fixed << std::setprecision(1) << total_seconds << " s");
    }

    auto mins = total_ms / MS_PER_MIN;
    auto remaining_ms = total_ms % MS_PER_MIN;

    if (total_ms < MS_PER_HOUR) {
        auto secs = remaining_ms / MS_PER_SEC;
        auto deci_secs = (remaining_ms % MS_PER_SEC) / 100;
        return STR(std::setfill('0')
                   << mins << ":" << std::setw(2) << secs << "." << deci_secs);
    }

    auto hrs = total_ms / MS_PER_HOUR;
    mins = (total_ms % MS_PER_HOUR) / MS_PER_MIN;
    auto secs = (total_ms % MS_PER_MIN) / MS_PER_SEC;

    return STR(std::setfill('0') << hrs << ":" << std::setw(2) << mins << ":"
                                 << std::setw(2) << secs);
}

fs::path create_temp_file(std::string const& prefix, std::string const& suffix);

fs::path get_user_cache_dir();

} // namespace util
