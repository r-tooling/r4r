#!/bin/bash

set -e

# Ensure R4R_UID and R4R_GID are set (via `-e R4R_UID=$(id -u)` etc)
: "${R4R_UID:?Need to set R4R_UID}"
: "${R4R_GID:?Need to set R4R_GID}"

OLD_UID="$(id -u r4r)"
OLD_GID="$(id -g r4r)"

if [ "$OLD_GID" != "$R4R_GID" ]; then
	if getent group "$HOST_GID" >/dev/null; then
		echo "WARNING: A group with GID $HOST_GID already exists; skipping groupmod for r4r." >&2
	else
		groupmod -g "$HOST_GID" r4r
	fi
fi

if [ "$OLD_UID" != "$R4R_UID" ]; then
	usermod -u "$R4R_UID" -g "$R4R_GID" r4r
fi

/usr/bin/find /home/r4r -user "$OLD_UID" -group "$OLD_GID" \
	-exec chown -h "$R4R_UID":"$R4R_GID" {} +

# Get the GID of the host's Docker group from the Docker socket
if [[ -e /var/run/docker.sock ]]; then
	HOST_DOCKER_GID=$(stat -c '%g' /var/run/docker.sock)
	groupmod -g "$HOST_DOCKER_GID" docker
else
	echo "Warning: Docker socket not found at /var/run/docker.sock"
fi

exec gosu r4r "$@"
