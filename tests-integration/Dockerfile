FROM ghcr.io/r-tooling/r4r:latest AS builder

WORKDIR /workspace
COPY . /workspace

# Build the project
RUN make clean install

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive \
  LANG=en_US.UTF-8

RUN apt-get update -y && \
  apt-get install -y --no-install-recommends locales tzdata && \
  locale-gen $LANG && \
  update-locale LANG=$LANG

# install docker
RUN apt-get update && \
  apt-get install -y --no-install-recommends \
  ca-certificates \
  curl \
  lsb-release \
  git \
  gnupg \
  gosu \
  r-base-dev \
  libicu-dev \
  sudo && \
  mkdir -p /etc/apt/keyrings && \
  curl -fsSL https://download.docker.com/linux/ubuntu/gpg | gpg --dearmor -o /etc/apt/keyrings/docker.gpg && \
  echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" | tee /etc/apt/sources.list.d/docker.list > /dev/null && \
  apt-get update && \
  apt-get install -y --no-install-recommends docker-ce-cli

# install R dependencies for the integration tests
RUN R -e 'install.packages("stringi", Ncpus=8)'

COPY --from=builder /workspace/build/r4r /usr/local/bin

ARG USERNAME=r4r
ARG USER_UID=1001
ARG USER_GID=1001

RUN groupadd -g $USER_GID $USERNAME && \
  useradd -u $USER_UID -g $USERNAME -m -s /bin/bash $USERNAME && \
  groupadd -f docker && \
  usermod -aG docker $USERNAME && \
  echo "$USERNAME ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/$USERNAME && \
  chmod 0440 /etc/sudoers.d/$USERNAME

COPY tests-integration/entrypoint.sh /
ENTRYPOINT ["/entrypoint.sh"]
CMD ["/usr/local/bin/r4r"]
