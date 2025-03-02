#ifndef INSTALL_R_PACKAGE_BUILDER_H
#define INSTALL_R_PACKAGE_BUILDER_H

#include "rpkg_database.h"
#include <vector>

// TODO: parameterize - CRAN mirror, temp folder
class InstallRPackageScriptBuilder {
  public:
    InstallRPackageScriptBuilder&
    set_plan(std::vector<std::vector<RPackage const*>> const& plan) {
        plan_ = plan;
        return *this;
    }

    InstallRPackageScriptBuilder& set_output(std::ostream& out) {
        out_ = &out;
        return *this;
    }

    InstallRPackageScriptBuilder& set_max_parallel(std::size_t max_parallel) {
        max_parallel_ = max_parallel;
        return *this;
    }

    void build() {
        if (max_parallel_ == 0) {
            throw std::runtime_error("Error: max_parallel cannot be zero.");
        }
        if (!out_) {
            throw std::runtime_error("Error: output is not set.");
        }

        // expand the plan so that no batch exceeds the max_parallel_ limit.
        expand_plan();

        write_script();
    }

  private:
    void expand_plan() {
        expanded_plan_.clear();
        expanded_plan_.reserve(plan_.size());

        for (auto const& batch : plan_) {
            if (batch.size() <= max_parallel_) {
                expanded_plan_.push_back(batch);
            } else {
                for (size_t i = 0; i < batch.size(); i += max_parallel_) {
                    size_t end = (i + max_parallel_ < batch.size())
                                     ? (i + max_parallel_)
                                     : batch.size();
                    expanded_plan_.emplace_back(batch.begin() + i,
                                                batch.begin() + end);
                }
            }
        }
    }

    /**
     * @brief Writes the entire R script: header, batch installations, and
     * footer.
     */
    void write_script() {
        write_header();

        size_t const total_batches = expanded_plan_.size();
        for (size_t batch_index = 0; batch_index < total_batches;
             ++batch_index) {
            write_batch(batch_index, total_batches,
                        expanded_plan_[batch_index]);
        }

        write_footer();
    }

    static constexpr char const* kRHeader =
        "cat('############################################################\\n')"
        "\n";

    void write_header() {
        *out_ << "#!/usr/bin/env Rscript\n\n";

        // ASCII box greeting
        *out_ << kRHeader;
        *out_ << "cat('# Starting installation...\\n');\n";
        *out_ << kRHeader;
        *out_ << "\n";

        // install the parallels package
        *out_ << "options(Ncpus=min(parallel::detectCores(), 32))\n\n"
              << "dir.create('" << tmp_lib_dir_ << "', recursive=TRUE)\n"
              << "install.packages('remotes', lib = '" << tmp_lib_dir_ << "')\n"
              << "on.exit(unlink('" << tmp_lib_dir_ << "', recursive = TRUE))\n"
              << "\n\n";
    }

    void write_batch(size_t batch_index, size_t total_batches,
                     std::vector<RPackage const*> const& batch_vec) {
        if (batch_vec.empty()) {
            return;
        }

        size_t batch_number = batch_index + 1;
        size_t batch_size = batch_vec.size();

        *out_ << kRHeader;
        *out_ << "cat('# Installing batch " << batch_number << "/"
              << total_batches << " with " << batch_size
              << " packages...\\n');\n";
        *out_ << kRHeader;
        *out_ << "\n";

        // build a shell command that installs all packages in parallel
        // (background jobs + wait).
        std::ostringstream shell_cmd;
        for (RPackage const* pkg : batch_vec) {
            std::string log_file =
                "/tmp/r4r-install-" + pkg->name + "-" + pkg->version + ".log";
            shell_cmd << "Rscript -e \\\"";

            std::visit(
                overloaded{
                    [&](RPackage::GitHub const& gh) {
                        shell_cmd << "require('remotes', lib.loc = '"
                                  << tmp_lib_dir_ << "');";
                        shell_cmd
                            << "remotes::install_github('" << gh.org << "/"
                            << gh.name << "', ref = '" << gh.ref
                            << "', upgrade = 'never', dependencies = FALSE)\n";
                    },
                    [&](RPackage::CRAN const&) {
                        shell_cmd << "require('remotes', lib.loc = '"
                                  << tmp_lib_dir_ << "');";
                        shell_cmd
                            << "remotes::install_version('" << pkg->name
                            << "', '" << pkg->version
                            << "', upgrade = 'never', dependencies = FALSE)\n";
                    },
                },
                pkg->repository);

            shell_cmd << "\\\"";
            shell_cmd << " > " + log_file + " 2>&1 & ";
        }
        shell_cmd << "wait";

        // Execute the shell command, capturing its exit status.
        *out_ << "status <- system(\"" << shell_cmd.str() << "\")\n"
              << "if (status != 0) {\n"
              << "  " << kRHeader;
        *out_ << "  cat('# Batch " << batch_number << "/" << total_batches
              << " FAILED.\\n');\n"
              << "  "
              << "  " << kRHeader;
        *out_ << "\n";

        // Print logs for each package
        for (RPackage const* pkg : batch_vec) {
            std::string log_file =
                "/tmp/r4r-install-" + pkg->name + "-" + pkg->version + ".log";
            *out_ << "  " << kRHeader;
            *out_ << "  cat('# Logs for package " << pkg->name << " version "
                  << pkg->version << " (" << log_file << ")" << "\\n');\n";
            *out_ << "  " << kRHeader;
            *out_ << "  cat(readLines('" << log_file << "'), sep='\\n')\n"
                  << "  cat('\\n')\n";
        }

        // Fail fast if the shell command had errors
        *out_ << "  quit(status = 1)\n"
              << "}\n\n";

        // Verify each package installed is the correct version.
        for (RPackage const* pkg : batch_vec) {
            std::string log_file =
                "/tmp/r4r-install-" + pkg->name + "-" + pkg->version + ".log";

            *out_ << "{\n"
                  << "  pkg_name <- '" << pkg->name << "'\n"
                  << "  pkg_ver  <- '" << pkg->version
                  << "'\n"
                  // clang-format off
                  << "  installed_ver <- tryCatch(as.character(packageVersion(pkg_name)), error = function(e) NA)\n"
                  // clang-format on
                  << "  if (is.na(installed_ver) || installed_ver != pkg_ver) "
                     "{\n";
            *out_ << "    " << kRHeader;
            *out_ << "    cat('# Error: Failed to install ', pkg_name, ' ', "
                     "pkg_ver, '(installed: ', installed_ver, ')', '\\n');\n";
            *out_ << "    " << kRHeader;
            *out_ << "    cat(readLines('" << log_file << "'), sep='\\n')\n"
                  << "    cat('\\n')\n"
                  << "    quit(status = 1)\n"
                  << "  }\n"
                  << "}\n\n";
        }

        *out_ << kRHeader;
        *out_ << "cat('# Successfully installed batch " << batch_number << "/"
              << total_batches << "\\n');\n";
        *out_ << kRHeader;
    }

    void write_footer() {
        size_t n = 0;
        for (auto const& batch : expanded_plan_) {
            n += batch.size();
        }

        *out_ << kRHeader;
        *out_ << "cat('# All " << n
              << " packages installed successfully.\\n');\n";
        *out_ << kRHeader;
    }

    std::vector<std::vector<RPackage const*>> plan_;
    std::vector<std::vector<RPackage const*>> expanded_plan_;

    std::string out_path_;
    std::ostream* out_{};
    std::size_t max_parallel_ = 1;
    std::string tmp_lib_dir_ = "/tmp/r4r-lib";
};

#endif // INSTALL_R_PACKAGE_BUILDER_H
