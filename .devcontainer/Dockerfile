ARG BASE_IMAGE=ubuntu-22.04

FROM mcr.microsoft.com/devcontainers/base:$BASE_IMAGE

# default to UTF-8 file.encoding
ENV LANG=en_US.UTF-8

# install build time dependencies
RUN apt update -y \
  && apt install -y \
    build-essential \
    cmake \
    gdb \
    ninja-build \
    rr

