#!/bin/bash
# scripts/push_base_image.sh — build and push the base image to ghcr.io
#
# Run this once (or when OpenOnload/TCPDirect deps change) from a machine
# that has the deps/ directory (e.g., gpu-dev2).
#
# Prerequisites:
#   1. Create a GitHub Personal Access Token (classic) with "write:packages" scope:
#      https://github.com/settings/tokens/new
#      → Select scope: "write:packages"
#      → Copy the token
#
#   2. Set the token as an environment variable:
#      export GITHUB_TOKEN=ghp_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
#
#   3. Set your GitHub username:
#      export GITHUB_USER=zhangw
#
# Usage:
#   [PROFILE=profiles/<name>.env] ./scripts/push_base_image.sh
#
# Or run directly on the remote:
#   ssh gpu-dev2 "cd ~/workspace/ilink3-demo && GITHUB_USER=zhangw GITHUB_TOKEN=ghp_xxx bash scripts/push_base_image.sh --local"

source "$(dirname "${BASH_SOURCE[0]}")/common.sh" 10

GITHUB_USER="${GITHUB_USER:?Set GITHUB_USER to your GitHub username}"
GITHUB_TOKEN="${GITHUB_TOKEN:?Set GITHUB_TOKEN to a PAT with write:packages scope}"
IMAGE="ghcr.io/${GITHUB_USER}/ilink3-base:latest"

if [[ "${1:-}" == "--local" ]]; then
    # Running directly on the build machine
    echo "[*] Building base image: ${IMAGE}"
    docker build -f Dockerfile.base -t "${IMAGE}" .

    echo "[*] Logging in to ghcr.io..."
    echo "${GITHUB_TOKEN}" | docker login ghcr.io -u "${GITHUB_USER}" --password-stdin

    echo "[*] Pushing ${IMAGE}..."
    docker push "${IMAGE}"

    echo "[*] Done. Base image pushed to ${IMAGE}"
else
    # Running from local machine — execute on remote
    echo "[*] Building and pushing base image on ${REMOTE_HOST}..."
    ssh "${SSH_OPTS[@]}" "${REMOTE_USER}@${REMOTE_HOST}" "
        cd ${REMOTE_PATH} &&
        docker build -f Dockerfile.base -t ${IMAGE} . 2>&1 &&
        echo '[*] Logging in to ghcr.io...' &&
        echo '${GITHUB_TOKEN}' | docker login ghcr.io -u '${GITHUB_USER}' --password-stdin &&
        echo '[*] Pushing ${IMAGE}...' &&
        docker push ${IMAGE} &&
        echo '[*] Done. Base image pushed.'
    "
fi
