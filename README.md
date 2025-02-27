# r4r

![Build](https://github.com/r-tooling/r4r/actions/workflows/main.yml/badge.svg)
[![Bugs](https://sonarcloud.io/api/project_badges/measure?project=r-tooling_r4r&metric=bugs&token=25f03a0bb9f860fa2b82118a65714715b9be3627)](https://sonarcloud.io/summary/new_code?id=r-tooling_r4r)
[![Code Smells](https://sonarcloud.io/api/project_badges/measure?project=r-tooling_r4r&metric=code_smells&token=25f03a0bb9f860fa2b82118a65714715b9be3627)](https://sonarcloud.io/summary/new_code?id=r-tooling_r4r)
[![Code Coverage](https://sonarcloud.io/api/project_badges/measure?project=r-tooling_r4r&metric=coverage&token=25f03a0bb9f860fa2b82118a65714715b9be3627)](https://sonarcloud.io/summary/new_code?id=r-tooling_r4r)

R4R is a tool for creating a reproducible environment from a dynamic program trace.

## Usage

```sh
$ r4r --help
Usage: r4r [OPTIONS] <command>

Options:
  -v, --verbose                   Make the tool more talkative (allow multiple)
  --docker-image-tag NAME         The docker image tag [default: r4r/test]
  --docker-container-name NAME    The docker container name [default: r4r-test]
  --result PATH                   Path to a result file
  --output PATH                   Path for the output
  --help                          Print this message

Positional arguments:
  command    The program to trace
```

## Build

Look at the `Makefile`:

```sh
$ make help
all                  Build and test the project (default)
configure            Configure CMake project
build                Build the project
test                 Run tests
coverage             Run tests with code coverage
install              Install the project
format               Format source code
lint                 Run static analysis
clean                Clean build artifacts
help                 Show this help message
```

### Dependencies

Look at the [.devcontainer/Dockerfile](.devcontainer/Dockerfile).

## Coding standards

- We try to follow [Google C++ code style](https://google.github.io/styleguide/cppguide.html)
