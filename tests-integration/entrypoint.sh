#!/bin/bash

set -e

# Ensure HOST_UID and HOST_GID are set (via `-e HOST_UID=$(id -u)` etc)
: "${HOST_UID:?Need to set HOST_UID}"
: "${HOST_GID:?Need to set HOST_GID}"
: "${HOST_USER:?Need to set HOST_USER}"

OLD_UID="$(id -u "$HOST_USER")"
OLD_GID="$(id -g "$HOST_USER")"

if [ "$OLD_GID" != "$HOST_GID" ]; then
	if getent group "$HOST_GID" >/dev/null; then
		echo "WARNING: A group with GID $HOST_GID already exists; skipping groupmod for $HOST_USER." >&2
	else
		groupmod -g "$HOST_GID" "$HOST_USER"
	fi
fi

if [ "$OLD_UID" != "$HOST_UID" ]; then
	if getent passwd "$HOST_GID" >/dev/null; then
		echo "WARNING: A user with UID $HOST_GID already exists; skipping usermod for $HOST_USER." >&2
	else
		usermod -u "$HOST_UID" "$HOST_USER"
	fi
fi

/usr/bin/find /home/"$HOST_USER" -user "$OLD_UID" -group "$OLD_GID" -exec chown -h "$HOST_UID":"$HOST_GID" {} \;

# Get the GID of the host's Docker group from the Docker socket
if [[ -e /var/run/docker.sock ]]; then
	if getent group docker >/dev/null; then
		HOST_DOCKER_GID=$(stat -c '%g' /var/run/docker.sock)
		groupmod -g "$HOST_DOCKER_GID" docker
	else
		echo "WARNING: docker group does not exists" >&2
	fi
else
	echo "WARNING: Docker socket not found at /var/run/docker.sock" >&2
fi

exec gosu "$HOST_USER" "$@"
