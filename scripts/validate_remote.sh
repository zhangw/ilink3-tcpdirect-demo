#!/bin/bash
# Usage: [PROFILE=profiles/<name>.env] ./scripts/validate_remote.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh" 5

echo "[*] Checking SSH to ${REMOTE_USER}@${REMOTE_HOST}..."
ssh "${SSH_OPTS[@]}" "${REMOTE_USER}@${REMOTE_HOST}" echo "SSH OK" || {
    echo "[!] SSH failed" >&2; exit 1
}

echo "[*] Checking required tools on remote..."
ssh "${SSH_OPTS[@]}" "${REMOTE_USER}@${REMOTE_HOST}" '
    missing=0
    for tool in docker make gcc unzip; do
        if command -v "$tool" >/dev/null 2>&1; then
            echo "  [ok] $tool"
        else
            echo "  [missing] $tool"
            missing=1
        fi
    done
    exit $missing
' || { echo "[!] Some required tools are missing on remote." >&2; exit 1; }
