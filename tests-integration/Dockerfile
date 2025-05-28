FROM ghcr.io/r-tooling/r4r:latest AS builder

WORKDIR /workspace
COPY . /workspace

# Build the project
RUN make clean install

# NOTE: if changed, make sure the user management lower is updated 
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
  LANG=en_US.UTF-8

# The upgrade is there because of https://github.com/r-tooling/r4r/issues/33
RUN apt-get update -y && \
  apt-get upgrade -y && \
  apt-get install -y --no-install-recommends locales tzdata && \
  locale-gen $LANG && \
  update-locale LANG=$LANG

# install docker
RUN apt-get update && \
  apt-get install -y --no-install-recommends \
  ca-certificates \
  curl \
  docker.io \
  lsb-release \
  git \
  gnupg \
  gosu \
  r-base-dev \
  r-recommended \
  libicu-dev \
  sudo \
  pandoc

# install R dependencies for the integration tests
RUN R -e 'install.packages(c("stringi", "ggplot2",  "rmarkdown"), Ncpus=8)'

COPY --from=builder /workspace/build/r4r /usr/local/bin

# -----------------------------
# User management
# -----------------------------

# remove the ubuntu user created in the container
# the reason is that it might collide with the UID/GID user running the integration tests
# 1000:1000 is common first user UID/GID on linux
RUN id ubuntu &>/dev/null && userdel -r ubuntu

# create a new that will run r4r

# NOTE: make sure it does not collide with any existing user inside the container
# otherwise things start to fail miserably
ARG USERNAME=r4r
ARG USER_UID=1001
ARG USER_GID=1001

RUN groupadd -g $USER_GID $USERNAME && \
  useradd -u $USER_UID -g $USERNAME -m -s /bin/bash $USERNAME && \
  groupadd -f docker && \
  usermod -aG docker $USERNAME && \
  echo "$USERNAME ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/$USERNAME && \
  chmod 0440 /etc/sudoers.d/$USERNAME

# allow user installed packages
RUN gosu "${USERNAME}" \
  R -e 'dir.create(unlist(strsplit(Sys.getenv("R_LIBS_USER"), .Platform$path.sep))[1L], recursive=TRUE)'

COPY tests-integration/entrypoint.sh /
ENTRYPOINT ["/entrypoint.sh"]
CMD ["/usr/local/bin/r4r"]
