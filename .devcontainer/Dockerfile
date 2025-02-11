ARG BASE_IMAGE=ubuntu-22.04

FROM mcr.microsoft.com/devcontainers/base:$BASE_IMAGE

# default to UTF-8 file.encoding
ENV LANG=en_US.UTF-8

# install build time dependencies
# note: we install gcovr via pip as the ubuntu package is too old
RUN apt-get update -y  && \ 
  apt-get install -y \
  build-essential \
  cmake \
  gdb \
  libcurl4-openssl-dev \
  ninja-build \
  pip \
  rr && \
  pip install gcovr==5.0
