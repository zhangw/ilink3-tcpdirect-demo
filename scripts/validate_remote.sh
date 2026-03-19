#!/bin/bash
# Usage: [PROFILE=profiles/other.env] ./scripts/validate_remote.sh
set -e

PROFILE=${PROFILE:-profiles/gpu-dev2.env}
source "${PROFILE}"

SSH_OPTS=(-o ConnectTimeout=5 -o BatchMode=yes)
[ -n "${REMOTE_SSH_KEY:-}" ] && SSH_OPTS+=(-i "${REMOTE_SSH_KEY}")

echo "[*] Checking SSH to ${REMOTE_USER}@${REMOTE_HOST}..."
ssh "${SSH_OPTS[@]}" "${REMOTE_USER}@${REMOTE_HOST}" echo "SSH OK" || {
    echo "[!] SSH failed" >&2; exit 1
}

echo "[*] Checking required tools on remote..."
TOOLS=(docker make gcc unzip)
for tool in "${TOOLS[@]}"; do
    ssh "${SSH_OPTS[@]}" "${REMOTE_USER}@${REMOTE_HOST}" \
        "command -v ${tool} >/dev/null 2>&1 && echo '  [ok] ${tool}' || echo '  [missing] ${tool}'"
done
