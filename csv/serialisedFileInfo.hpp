#pragma once
#include "../middleend/middleEnd.hpp"
#include <fstream>
#include <string>
#include <type_traits>

namespace CSV {
// src:
// https://stackoverflow.com/questions/2896600/how-to-replace-all-occurrences-of-a-character-in-string
template <class T, class Other = T>
inline T&& replaceAll(T&& source, const Other&& from, const Other&& to) {
    using noref = std::remove_reference_t<T>;
    noref newString{};
    newString.reserve(source.length()); // avoids a few memory allocations

    typename noref::size_type lastPos = 0;
    typename noref::size_type findPos;

    while (noref::npos != (findPos = source.find(from, lastPos))) {
        newString.append(source, lastPos, findPos - lastPos);
        newString += to;
        lastPos = findPos + from.length();
    }

    // Care for the rest after last occurrence
    newString += source.substr(lastPos);

    source.swap(newString);
    return std::forward<T>(source);
}

inline void write(std::ostream& out, std::string_view str,
                  bool writeComma = true) {
    if (str.find(",") != str.npos || str.find("\n") != str.npos ||
        str.find("\"") != str.npos) {
        out << "\""
            << replaceAll<std::string, std::string_view>(std::string{str}, "\"",
                                                         "\"\"\"")
            << "\"";
    } else {
        out << str;
    }
    if (writeComma) {
        out << ",";
    }
}

inline void write(std::ostream& out, bool str, bool writeComma = true) {
    if (str) {
        out << 1;
    } else {
        out << 0;
    }
    if (writeComma) {
        out << ",";
    }
}
template <std::integral T>
inline void write(std::ostream& out, T&& str, bool writeComma = true) {
    out << str;
    if (writeComma) {
        out << ",";
    }
}

template <class Contained, class InterpretAs = Contained>
inline void write(std::ostream& out, const std::optional<Contained>& str,
                  bool writeComma = true) {
    if (str.has_value()) {
        write(out, static_cast<InterpretAs>(str.value()), false);
    }
    if (writeComma) {
        out << ",";
    }
}
void serializeFiles(
    const std::unordered_map<
        absFilePath, std::unique_ptr<middleend::file_info>>&
        files,
    std::filesystem::path csvPath);

std::unordered_map<absFilePath, middleend::file_info> deSerializeFiles(std::filesystem::path csvPath);

std::vector<std::string> deSerializeEnv(std::filesystem::path csvPath);
void serializeEnv(const std::vector<std::string>& env,
                  std::filesystem::path csvPath);

std::filesystem::path deSerializeWorkdir(std::filesystem::path csvPath);
void serializeWorkdir(const std::filesystem::path& path,
                      std::filesystem::path csvPath);
} // namespace CSV