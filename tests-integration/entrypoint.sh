#!/bin/bash

set -e

# Ensure HOST_UID and HOST_GID are set (via `-e HOST_UID=$(id -u)` etc)
: "${HOST_UID:?Need to set HOST_UID}"
: "${HOST_GID:?Need to set HOST_GID}"
: "${HOST_USER:?Need to set HOST_USER}"

OLD_UID="$(id -u "$HOST_USER")"
OLD_GID="$(id -g "$HOST_USER")"

if [ "$OLD_GID" != "$HOST_GID" ]; then
	(groupmod -g "$HOST_GID" "$HOST_USER" || true)
fi

if [ "$OLD_UID" != "$HOST_UID" ]; then
	(usermod -u "$HOST_UID" "$HOST_USER" || true)
fi

/usr/bin/find /home/"$HOST_USER" -user "$OLD_UID" -group "$OLD_GID" -exec chown -h "$HOST_UID":"$HOST_GID" {} \;

# Get the GID of the host's Docker group from the Docker socket
if [[ -e /var/run/docker.sock ]]; then
	HOST_DOCKER_GID=$(stat -c '%g' /var/run/docker.sock)
	(groupmod -g "$HOST_DOCKER_GID" docker || true)
else
	echo "WARNING: Docker socket not found at /var/run/docker.sock" >&2
fi

exec gosu "$HOST_USER" "$@"
