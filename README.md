# r4r

![Build](https://github.com/r-tooling/r4r/actions/workflows/main.yml/badge.svg)
[![Bugs](https://sonarcloud.io/api/project_badges/measure?project=r-tooling_r4r&metric=bugs&token=25f03a0bb9f860fa2b82118a65714715b9be3627)](https://sonarcloud.io/summary/new_code?id=r-tooling_r4r)
[![Code Smells](https://sonarcloud.io/api/project_badges/measure?project=r-tooling_r4r&metric=code_smells&token=25f03a0bb9f860fa2b82118a65714715b9be3627)](https://sonarcloud.io/summary/new_code?id=r-tooling_r4r)
[![Code Coverage](https://sonarcloud.io/api/project_badges/measure?project=r-tooling_r4r&metric=coverage&token=25f03a0bb9f860fa2b82118a65714715b9be3627)](https://sonarcloud.io/summary/new_code?id=r-tooling_r4r)

R4R is a tool for creating a reproducible environment from a dynamic program trace.

[Fill the survey about re-executability and reproducibility of R code](https://framaforms.org/re-executability-and-reproducibility-of-r-notebooks-and-scripts-1754417915)

[Article at ACM REP 2025](https://www.pdonatbouillud.com/donat-r4r-rep-2025.pdf)

## Getting started 

Note that we currently only support Debian and Ubuntu.

### Install from source

Clone the repository:

```bash 
git clone git@github.com:r-tooling/r4r.git
```

Make sure you have a C++ compiler supporting at least C++21 and [CMake](https://cmake.org/).

Go into the `r4r` directory, compile and install:

```bash
cd r4r
make install
```

You're now good to go!

### Pre-built binary

Download the r4r binary for your architecture from the [release page](https://github.com/r-tooling/r4r/releases).

### Run it on a R code

Run the tool on a R notebook that outputs a HTML file:

```bash
r4r R -e "rmarkdown::render('path/to/notebook.Rmd')" --output output --result notebook.html
```

This will create an `output` directory with a Docker file, a Makefile, and other auxiliary files then build a Docker image.with tag `r4r/test` all the necessary data, R package, system package dependencies, and then run the Docker image as a container called `r4r-test` to re-execute the notebook. 
The result will be saved in `output/result` if you want to check if it is exactly reproducible. 

You can skip the building of the Docker image by passing `--skip-make` to `r4r`.
In that case, you will have to go to the `output` directory and run `make build` to 
build the Docker image and `make copy` to run it and get the result.

### Uploading the image to a repository 

If you want to upload the image to a repository, you can use the `docker push` command:

```bash
docker push r4r/test
```

If you want to manually export the image to save it in some archive, you can do:

```bash
docker save r4r/test -o r4r-test.tar
```

You most likely want to compress that tar archive.

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

## R Package and RStudio Addins

Have a a look at [r4rwrapper](https://github.com/r-tooling/r4rwrapper).

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

## Integration tests

Integration tests can be run in two modes:

- local
- in docker container `ghcr.io/r-tooling/r4r-it`

```sh
make -C tests-integration <test_name> [LOCAL=1]
```

For example:

```sh
make -C tests-integration r-hello-world
```

If running in the local mode (`LOCAL=1`), make sure the project is built before.
Also, make sure that you have all the dependencies the test requires (i.e. all the R packages).
You can do that by trying a test run:

```sh
make -C tests-integration/r-hello-world test-run
```

Missing R packages could be installed using the `setup` target:

```sh
make -C tests-integration/r-hello-world setup
```

If running in the container, the makefile should built the container before running the test.
The container could be built manually by

```sh
make -C tests-integration docker-image
```

The name of the test is the name of a directory inside the `tests-integration` directory.
The `r-kaggle` can only run locally, as it would make the container too big

Note:

- The final check for the generated artifacts is rather simple, using a diff
- It is possible that a newer version of a library changes slightly how the generated artifact looks like (e.g., changes the JS code in the preamble of the HTML file).

## Coding standards

- We try to follow [Google C++ code style](https://google.github.io/styleguide/cppguide.html)
