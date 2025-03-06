#ifndef ARCHIVE_H
#define ARCHIVE_H

#include "process.h"
#include "util_fs.h"
#include <fstream>

template <typename FileCollection>
void create_tar_archive(fs::path const& archive, FileCollection const& files) {
    TempFile temp_file{"r4r-tar", ".txt"};
    {
        std::ofstream temp{*temp_file};
        for (auto const& file : files) {
            temp << file.string() << '\n';
            LOG(DEBUG) << "Adding to tar: " << file;
        }
    }

    auto out = Command("tar")
                   .arg("-c")
                   .arg("-f")
                   .arg(archive.string())
                   .arg("--verbose")
                   .arg("--absolute-names")
                   .arg("--same-permissions")
                   .arg("--same-owner")
                   .arg("--files-from")
                   .arg(*temp_file)
                   .output(true);

    out.check_success(STR("Error creating tar archive: " << archive));
}

#endif // ARCHIVE_H
