#!/bin/bash
# Usage: [PROFILE=profiles/<name>.env] ./scripts/sync_remote.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh" 10

echo "[*] Syncing to ${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_PATH}..."
rsync -avz \
    --rsync-path="mkdir -p ${REMOTE_PATH} && rsync" \
    --exclude='.git/' \
    --exclude='.worktrees' \
    --exclude='include/' \
    --exclude='lib/' \
    --exclude='*.o' \
    --exclude='ilink3_client' \
    --exclude='ilink3_client_static' \
    --exclude='ilink3_server' \
    --exclude='test_base64url' \
    --exclude='test_hmac_signing' \
    --exclude='test_sbe_framing' \
    --exclude='test_validation' \
    --exclude='test_frame_extract' \
    --exclude='perf_session' \
    --exclude='tests/results/' \
    --exclude='test-results/' \
    -e "${SSH_CMD}" \
    ./ "${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_PATH}/"
echo "[*] Sync complete."
