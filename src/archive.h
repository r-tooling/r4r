#ifndef ARCHIVE_H
#define ARCHIVE_H

#include "fs.h"
#include "process.h"
#include <fstream>

template <typename FileCollection>
void create_tar_archive(fs::path const& archive, FileCollection const& files) {
    TempFile temp_file{"r4r-tar", ".txt"};
    {
        std::ofstream temp{*temp_file};
        for (auto const& file : files) {
            temp << file.string() << '\n';
        }
    }

    std::vector<std::string> command = {
        "tar",     "--absolute-names", "--preserve-permissions",
        "-cvf",    archive.string(),   "--files-from",
        *temp_file};

    auto [tar_out, exit_code] = execute_command(command, true);
    // FIXME: this should be included in the execute_command
    if (exit_code != 0) {
        std::string msg = STR("Error creating tar archive: "
                              << archive.string() << ". tar exit code:  "
                              << exit_code << "\nOutput:\n"
                              << tar_out);
        throw std::runtime_error(msg);
    }
}

#endif // ARCHIVE_H
