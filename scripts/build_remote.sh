#!/bin/bash
# Usage: [PROFILE=profiles/<name>.env] ./scripts/build_remote.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh" 10

echo "[*] Building and testing on ${REMOTE_HOST}..."
ssh "${SSH_OPTS[@]}" "${REMOTE_USER}@${REMOTE_HOST}" "
    cd ${REMOTE_PATH} &&
    docker build -t ilink3-demo . 2>&1 &&
    echo '[*] Build done — running loopback test...' &&
    docker run --rm \
        --shm-size=512m \
        --ulimit memlock=-1 \
        --cap-add=IPC_LOCK \
        --cap-add=NET_ADMIN \
        ilink3-demo
"
