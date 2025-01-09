#include "util.hpp"

namespace util {

std::string escape_cmd_arg(std::string const& arg) {
    if (arg.empty()) {
        return "''"; // Handle empty strings
    }

    bool needs_quoting = false;
    std::string quoted_arg;
    for (char c : arg) {
        if (std::isspace(c) || c == '\'' || c == '\\' || c == '"' || c == '$' ||
            c == '`' || c == ';' || c == '&' || c == '|' || c == '*' ||
            c == '?' || c == '[' || c == ']' || c == '(' || c == ')' ||
            c == '<' || c == '>' || c == '#' || c == '!') {
            needs_quoting = true;
        }

        if (c == '\'') {
            quoted_arg += "'\\''";
        } else {
            quoted_arg += c;
        }
    }

    if (needs_quoting) {
        quoted_arg = "'" + quoted_arg + "'";
    }

    return quoted_arg;
}

std::string escape_env_var_definition(std::string env) {
    size_t pos = env.find('=');

    if (pos != std::string_view::npos) {
        if (pos + 1 >= env.length() || env[pos + 1] != '"') {
            size_t last_non_ws = env.find_last_not_of(" \t\n\r\f\v");
            if ((last_non_ws != std::string::npos && env[last_non_ws] != '"') ||
                last_non_ws == std::string::npos) {
                env.insert(pos + 1, "\"");
                env += "\"";
            }
        }
    }

    return env;
}

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
