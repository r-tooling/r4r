#include "util.hpp"

namespace util {

bool is_sub_path(fs::path const& path, fs::path const& base) {
    const auto mismatch =
        std::mismatch(path.begin(), path.end(), base.begin(), base.end());
    return mismatch.second == base.end();
}

std::string execute_command(std::string const& command) {
    std::array<char, 128> buffer{};
    std::string result;

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to run command: " + command);
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }

    if (pclose(pipe) != 0) {
        throw std::runtime_error("Command execution failed: " + command);
    }

    return result;
}

} // namespace util
