#!/bin/sh

set -e

# Get the GID of the host's Docker group from the Docker socket
if [ -e /var/run/docker.sock ]; then
	HOST_DOCKER_GID=$(stat -c '%g' /var/run/docker.sock)
	groupmod -g $HOST_DOCKER_GID docker
else
	echo "Warning: Docker socket not found at /var/run/docker.sock"
fi

exec gosu r4r "$@"
