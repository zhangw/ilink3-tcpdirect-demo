#!/bin/bash
# scripts/common.sh — sourced by all remote scripts.
# Sets: PROFILE, REMOTE_HOST, REMOTE_USER, REMOTE_PATH, SSH_OPTS, SSH_CMD
# Usage: source "$(dirname "${BASH_SOURCE[0]}")/common.sh" [connect_timeout]
set -euo pipefail

_CONNECT_TIMEOUT="${1:-10}"

# Resolve PROFILE: caller-supplied, or auto-detect from profiles/
if [[ -z "${PROFILE:-}" ]]; then
    _REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    _profiles=()
    for _f in "${_REPO_ROOT}/profiles/"*.env; do
        [[ -f "$_f" ]] && [[ "$_f" != */example.env ]] && _profiles+=("$_f")
    done
    case "${#_profiles[@]}" in
        0) echo "[!] No profiles found in profiles/ — create one first." >&2; exit 1 ;;
        1) PROFILE="${_profiles[0]}"
           echo "[*] Using profile: ${PROFILE}" ;;
        *) echo "[!] Multiple profiles found — set PROFILE=<path> to choose:" >&2
           printf '    %s\n' "${_profiles[@]}" >&2; exit 1 ;;
    esac
fi

[[ -f "${PROFILE}" ]] || { echo "[!] Profile not found: ${PROFILE}" >&2; exit 1; }
# shellcheck source=/dev/null
source "${PROFILE}"

SSH_OPTS=(-o "ConnectTimeout=${_CONNECT_TIMEOUT}" -o BatchMode=yes)

# SSH_CMD string for rsync -e (avoids array word-splitting with key paths)
SSH_CMD="ssh -o ConnectTimeout=${_CONNECT_TIMEOUT} -o BatchMode=yes"

if [[ -n "${REMOTE_SSH_KEY:-}" ]]; then
    SSH_OPTS+=(-i "${REMOTE_SSH_KEY}")
    SSH_CMD+=" -i ${REMOTE_SSH_KEY}"
fi
