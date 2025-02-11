FROM r-tooling/r4r-dev AS builder

WORKDIR /workspace
COPY . /workspace

# Build the project
RUN make install

FROM ubuntu:22.04

# install runtime time dependencies
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        lsb-release \
        gnupg \
        gosu && \
    mkdir -p /etc/apt/keyrings && \
    curl -fsSL https://download.docker.com/linux/ubuntu/gpg | gpg --dearmor -o /etc/apt/keyrings/docker.gpg && \
    echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" | tee /etc/apt/sources.list.d/docker.list > /dev/null && \
    apt-get update && \
    apt-get install -y --no-install-recommends docker-ce-cli

WORKDIR /

COPY --from=builder /workspace/build/r4r /usr/local/bin

ARG USERNAME=r4r
ARG USER_UID=1000
ARG USER_GID=1000

RUN groupadd -g $USER_GID $USERNAME && \
    useradd -u $USER_UID -g $USERNAME -m -s /bin/bash $USERNAME && \
    groupadd docker && \
    usermod -aG docker $USERNAME

COPY entrypoint.sh /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
CMD ["/usr/local/bin/r4r"]
