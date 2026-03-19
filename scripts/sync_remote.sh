#!/bin/bash
# Usage: [PROFILE=profiles/other.env] ./scripts/sync_remote.sh
set -e

PROFILE=${PROFILE:-profiles/gpu-dev2.env}
source "${PROFILE}"

SSH_OPTS=(-o ConnectTimeout=10)
[ -n "${REMOTE_SSH_KEY:-}" ] && SSH_OPTS+=(-i "${REMOTE_SSH_KEY}")

echo "[*] Syncing to ${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_PATH}..."
ssh "${SSH_OPTS[@]}" "${REMOTE_USER}@${REMOTE_HOST}" "mkdir -p ${REMOTE_PATH}"
rsync -avz \
    --exclude='.git/' \
    --exclude='.worktrees' \
    --exclude='include/' \
    --exclude='lib/' \
    --exclude='*.o' \
    --exclude='ilink3_client' \
    --exclude='ilink3_client_static' \
    --exclude='ilink3_server' \
    -e "ssh ${SSH_OPTS[*]}" \
    ./ "${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_PATH}/"
echo "[*] Sync complete."
