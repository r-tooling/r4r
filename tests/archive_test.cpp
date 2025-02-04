#include "archive.h"
#include <gtest/gtest.h>

TEST(UtilTest, CreateTarArchiveTest) {
    auto temp_dir = fs::temp_directory_path() / "tar_archive_test";
    if (fs::exists(temp_dir)) {
        fs::remove_all(temp_dir);
    }
    fs::create_directory(temp_dir);

    std::array<fs::path, 5> files;
    std::string files_str;
    for (size_t i = 1; i <= files.size(); i++) {
        auto f = temp_dir / ("file" + std::to_string(i) + ".txt");
        std::ofstream fs(f);
        fs << "file" << i << ".";
        fs.close();
        files_str += f.string() + "\n";
        files[i - 1] = f;
    }

    auto archive = temp_dir / "archive.tar";

    create_tar_archive(archive, files);

    EXPECT_TRUE(fs::exists(archive));

    auto out = Command("tar")
                   .arg("tf")
                   .arg(archive.string())
                   .arg("--absolute-names")
                   .output();

    EXPECT_EQ(out.exit_code, 0);
    EXPECT_EQ(out.stdout_data, files_str);
    EXPECT_EQ(out.stderr_data, "");

    fs::remove_all(temp_dir);
}
