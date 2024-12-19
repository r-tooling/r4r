#include "serialisedFileInfo.hpp"
#include "../backend/optionals.hpp"
#include "../external/csv.hpp"
#include "../stringHelpers.hpp"
void CSV::serializeFiles(
    const std::unordered_map<
        absFilePath, std::unique_ptr<middleend::MiddleEndState::file_info>>&
        files,
    std::filesystem::path csvPath) {

    std::ofstream outputCsv{csvPath, std::ios::openmode::_S_trunc |
                                         std::ios::openmode::_S_out};

    outputCsv << "RealPath,WasEverCreated,WasEverDeleted,IsCurrentlyOnTheDisk,"
                 "WasInitiallyOnTheDisk,FileType,AccessedAs"
              << std::endl;

    for (auto& [_, ptr] : files) {
        const middleend::MiddleEndState::file_info& obj = *ptr;

        write(outputCsv, obj.realpath.string());
        write(outputCsv, obj.wasEverCreated);
        write(outputCsv, obj.wasEverDeleted);
        write(outputCsv, obj.isCurrentlyOnTheDisk);
        write(outputCsv, obj.wasInitiallyOnTheDisk);
        write(outputCsv, obj.type);

        std::stringstream accesses;
        bool first = true;
        for (const auto& access : obj.accessibleAs) {
            if (!first)
                accesses << std::endl;
            first = false;
            write(accesses, access.relPath.string());
            write(accesses, access.workdir.string());
            write(accesses, access.executable);
            write(accesses, access.flags, false);
        }
        write(outputCsv, accesses.view(), false);
        outputCsv << std::endl;
    }
}

template <typename Test, template <typename...> class Ref>
struct is_specialization_t : std::false_type {};

template <template <typename...> class Ref, typename... Args>
struct is_specialization_t<Ref<Args...>, Ref> : std::true_type {};

template <class T>
T do_parse(std::string_view str) {
    return static_cast<T>(str);
}
template <class T>
requires requires { requires std::is_arithmetic_v<T>; }
T do_parse(csv::CSVField col) { return col.get<T>(); }
template <class T>
T do_parse(csv::CSVField col) {
    static_assert(!std::is_arithmetic_v<T>);
    return do_parse<T>(col.get_sv());
}
template <>
bool do_parse<bool>(csv::CSVField col) {
    return col.get<uint8_t>();
}

template <>
std::filesystem::path do_parse<std::filesystem::path>(csv::CSVField col) {
    return do_parse<std::filesystem::path>(col.get_sv());
}

template <class T>
requires requires {
    requires is_specialization_t<std::remove_const_t<T>, std::optional>::value;
}
T do_parse(csv::CSVField str) {
    if (str.get_sv().empty()) {
        return std::nullopt;
    } else {
        return {do_parse<typename T::value_type>(str)};
    }
}

std::unordered_map<absFilePath,
                   std::unique_ptr<middleend::MiddleEndState::file_info>>
CSV::deSerializeFiles(std::filesystem::path csvPath) {
    using namespace csv;
    CSVFormat format{};
    format.delimiter(',').quote('"').header_row(0).variable_columns(
        VariableColumnPolicy::THROW);
    CSVFormat accessformat{};
    accessformat.delimiter(',')
        .quote('"')
        .column_names({"path", "dir", "exec", "flags"})
        .variable_columns(VariableColumnPolicy::THROW);

    CSVReader inputCSV(csvPath.string(), format);

    using FI = middleend::MiddleEndState::file_info;

    std::unordered_map<absFilePath, std::unique_ptr<FI>> results;

    for (auto& row : inputCSV) {
        auto realPath = do_parse<decltype(FI::realpath)>(row["RealPath"]);
        auto accessCSV = row["AccessedAs"].get_sv();
        std::unordered_set<middleend::access_info> acesses;

        if (!accessCSV.empty()) {
            accessCSV = ltrim(accessCSV, "\n");
            // needs the wrapping in std string due to bugs.
            auto accessParse = parse(std::string{accessCSV}, accessformat);
            for (auto& row : accessParse) {
                acesses.emplace(
                    -1,
                    do_parse<decltype(middleend::access_info::relPath)>(
                        row["path"]),
                    do_parse<decltype(middleend::access_info::flags)>(
                        row["flags"]),
                    do_parse<decltype(middleend::access_info::executable)>(
                        row["exec"]),
                    do_parse<decltype(middleend::access_info::workdir)>(
                        row["dir"]));
            }
        }

        results.try_emplace(
            realPath,
            std::make_unique<middleend::MiddleEndState::file_info>(
                realPath, std::move(acesses),
                do_parse<decltype(FI::wasEverCreated)>(row["WasEverCreated"]),
                do_parse<decltype(FI::wasEverDeleted)>(row["WasEverDeleted"]),
                do_parse<decltype(FI::isCurrentlyOnTheDisk)>(
                    row["IsCurrentlyOnTheDisk"]),
                do_parse<decltype(FI::wasInitiallyOnTheDisk)>(
                    row["WasInitiallyOnTheDisk"]),
                optTransform(
                    do_parse<std::optional<size_t>>(row["FileType"]),
                    [](size_t v) {
                        return static_cast<decltype(FI::type)::value_type>(v);
                    }),
                false));
    }

    return results;
}

std::vector<std::string> CSV::deSerializeEnv(std::filesystem::path csvPath) {
    std::vector<std::string> env;

    csv::CSVFormat format{};
    format.delimiter(',').quote('"').header_row(-1).variable_columns(
        csv::VariableColumnPolicy::KEEP);

    csv::CSVReader reader{csvPath.string(), format};
    for (auto& row : reader) {
        for (auto& col : row) {
            env.emplace_back(col.get_sv());
        }
    }
    return env;
}

void CSV::serializeEnv(const std::vector<std::string>& env,
                       std::filesystem::path csvPath) {
    std::ofstream res{csvPath, std::ios::openmode::_S_trunc |
                                   std::ios::openmode::_S_out};
    auto writer = csv::make_csv_writer(res);
    writer << env;
}

std::filesystem::path CSV::deSerializeWorkdir(std::filesystem::path csvPath) {
    std::string contents;
    std::ifstream input(csvPath);
    input >> contents;
    return contents;
}
void CSV::serializeWorkdir(const std::filesystem::path& path,
                           std::filesystem::path csvPath) {
    std::ofstream output(csvPath, std::ios::openmode::_S_trunc |
                                      std::ios::openmode::_S_out);
    output << path.string();
}
