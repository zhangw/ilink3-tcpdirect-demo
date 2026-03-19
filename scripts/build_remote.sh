#!/bin/bash
# Usage: [PROFILE=profiles/other.env] ./scripts/build_remote.sh
set -e

PROFILE=${PROFILE:-profiles/gpu-dev2.env}
source "${PROFILE}"

SSH_OPTS=(-o ConnectTimeout=10)
[ -n "${REMOTE_SSH_KEY:-}" ] && SSH_OPTS+=(-i "${REMOTE_SSH_KEY}")

echo "[*] Building Docker image on ${REMOTE_HOST}..."
ssh "${SSH_OPTS[@]}" "${REMOTE_USER}@${REMOTE_HOST}" \
    "cd ${REMOTE_PATH} && docker build -t ilink3-demo . 2>&1"

echo "[*] Running loopback test on ${REMOTE_HOST}..."
ssh "${SSH_OPTS[@]}" "${REMOTE_USER}@${REMOTE_HOST}" \
    "docker run --rm \
        --shm-size=512m \
        --ulimit memlock=-1 \
        --cap-add=IPC_LOCK \
        --cap-add=NET_ADMIN \
        ilink3-demo"
