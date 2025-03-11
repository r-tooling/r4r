
#include "dockerfile.h"
#include <gtest/gtest.h>

TEST(DockerFileBuilderTest, EnvMultipleKeyValues) {
    DockerFileBuilder builder("ubuntu:latest", fs::path("/tmp/context"));
    builder.env("KEY", "value");
    builder.env({{"KEY1", "value1"}, {"KEY2", "value2"}, {"KEY3", "value3"}});
    builder.env("MAKEOVERRIDES", "${-*-command-variables-*-}");

    auto dockerfile = builder.build();
    std::string expected = R"(FROM ubuntu:latest
ENV KEY=value

ENV KEY1=value1 \
  KEY2=value2 \
  KEY3=value3

ENV MAKEOVERRIDES='${-*-command-variables-*-}'

)";

    EXPECT_EQ(dockerfile.dockerfile(), expected);
}
