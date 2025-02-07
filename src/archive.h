#ifndef ARCHIVE_H
#define ARCHIVE_H

#include "util_fs.h"
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

    auto out = Command("tar")
                   .arg("--absolute-names")
                   .arg("--preserve-permissions")
                   .arg("-cvf")
                   .arg(archive.string())
                   .arg("--files-from")
                   .arg(*temp_file)
                   .output(true);

    out.check_success(STR("Error creating tar archive: " << archive));
}

#endif // ARCHIVE_H
