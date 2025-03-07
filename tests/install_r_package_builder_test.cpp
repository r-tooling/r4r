#include "install_r_package_builder.h"
#include "rpkg_database.h"

#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

class InstallRPackageScriptBuilderTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Create some test packages
        cran_pkg1_ = std::make_unique<RPackage>(
            RPackageBuilder{"pkg1", "1.0.0"}.build());
        cran_pkg2_ = std::make_unique<RPackage>(
            RPackageBuilder{"pkg2", "2.0.0"}.build());
        cran_pkg3_ = std::make_unique<RPackage>(
            RPackageBuilder{"pkg3", "3.0.0"}.build());
        cran_pkg4_ = std::make_unique<RPackage>(
            RPackageBuilder{"pkg4", "4.0.0"}.build());
        github_pkg_ = std::make_unique<RPackage>(
            RPackageBuilder{"github_pkg", "1.0.0"}
                .repository(RPackage::GitHub{
                    .org = "user", .name = "repo", .ref = "HEAD"})
                .build());
    }

  public:
    InstallRPackageScriptBuilder create_builder() {
        InstallRPackageScriptBuilder builder;
        builder.set_output(output_);
        return builder;
    }

    std::stringstream output_;
    std::unique_ptr<RPackage> cran_pkg1_;
    std::unique_ptr<RPackage> cran_pkg2_;
    std::unique_ptr<RPackage> cran_pkg3_;
    std::unique_ptr<RPackage> cran_pkg4_;
    std::unique_ptr<RPackage> github_pkg_;
};

TEST_F(InstallRPackageScriptBuilderTest, EmptyPlanProducesBasicScript) {
    auto builder = create_builder();

    std::vector<std::vector<RPackage const*>> empty_plan;
    builder.set_plan(empty_plan).build();

    std::string result = output_.str();

    // Check that we have a header even with an empty plan
    EXPECT_TRUE(string_contains(result, "#!/usr/bin/env Rscript"));
    EXPECT_TRUE(string_contains(result, "Starting installation"));
    EXPECT_TRUE(
        string_contains(result, "All 0 packages installed successfully"));
}

TEST_F(InstallRPackageScriptBuilderTest, SingleBatchPlanProducesCorrectScript) {
    auto builder = create_builder();

    std::vector<std::vector<RPackage const*>> plan = {
        {cran_pkg1_.get(), cran_pkg2_.get()}};

    builder.set_plan(plan).set_max_parallel(32).build();

    std::string result = output_.str();

    // Check for batch installation
    EXPECT_TRUE(
        string_contains(result, "Installing batch 1/1 with 2 packages"));
    EXPECT_TRUE(string_contains(result, "pkg1"));
    EXPECT_TRUE(string_contains(result, "1.0.0"));
    EXPECT_TRUE(string_contains(result, "pkg2"));
    EXPECT_TRUE(string_contains(result, "2.0.0"));
    EXPECT_TRUE(string_contains(result, "install_version"));
}

TEST_F(InstallRPackageScriptBuilderTest, MultipleBatchesProduceCorrectScript) {
    auto builder = create_builder();

    std::vector<std::vector<RPackage const*>> plan = {
        {cran_pkg1_.get()}, {cran_pkg2_.get(), github_pkg_.get()}};

    builder.set_plan(plan).set_max_parallel(32).build();

    std::string result = output_.str();

    // Check for first batch
    EXPECT_TRUE(
        string_contains(result, "Installing batch 1/2 with 1 packages"));
    EXPECT_TRUE(string_contains(result, "pkg1"));

    // Check for second batch
    EXPECT_TRUE(
        string_contains(result, "Installing batch 2/2 with 2 packages"));
    EXPECT_TRUE(string_contains(result, "pkg2"));
    EXPECT_TRUE(string_contains(result, "github_pkg"));

    // Check for GitHub installation
    EXPECT_TRUE(string_contains(result, "install_github"));
    EXPECT_TRUE(string_contains(result, "user/repo"));
}

TEST_F(InstallRPackageScriptBuilderTest, ParallelLimitExpandsCorrectly) {
    auto builder = create_builder();

    // Create a batch with 5 packages
    std::vector<RPackage const*> large_batch = {
        cran_pkg1_.get(), cran_pkg2_.get(), cran_pkg3_.get(), cran_pkg4_.get(),
        github_pkg_.get()};

    std::vector<std::vector<RPackage const*>> plan = {large_batch};

    // Set max_parallel to 2, which should split into 3 batches
    builder.set_plan(plan).set_max_parallel(2).build();

    std::string result = output_.str();

    // Check for the expanded batches
    EXPECT_TRUE(
        string_contains(result, "Installing batch 1/3 with 2 packages"));
    EXPECT_TRUE(
        string_contains(result, "Installing batch 2/3 with 2 packages"));
    EXPECT_TRUE(
        string_contains(result, "Installing batch 3/3 with 1 packages"));
}
