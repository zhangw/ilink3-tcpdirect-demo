#!/bin/bash
#
# run_perf.sh — performance test runner for iLink3 loopback
#
# All thresholds are configurable via environment variables.
# Runs inside the Docker container with ZF b2b emulator.
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RESULTS_DIR="${SCRIPT_DIR}/../results"
mkdir -p "${RESULTS_DIR}"

SERVER_IP="${SERVER_IP:-10.0.0.1}"
CLIENT_IP="${CLIENT_IP:-10.0.0.2}"
PORT="${PORT:-10001}"
ROUNDS="${PERF_ROUNDS:-100}"

# Configurable thresholds (ms) — set to 0 to disable
# NOTE: ZF b2b emulator polls at ~100ms, so RTTs are ~100ms minimum.
THRESH_NEGOTIATE="${THRESH_NEGOTIATE_MS:-500}"
THRESH_ESTABLISH="${THRESH_ESTABLISH_MS:-500}"
THRESH_SEQUENCE="${THRESH_SEQUENCE_MS:-500}"
THRESH_TOTAL="${THRESH_TOTAL_MS:-120000}"

EMU_ATTR="emu=1;emu_shmname=ilink3_perf;max_sbufs=128"

# Setup loopback IPs (may already exist from integration tests)
ip addr add ${SERVER_IP}/24 dev lo 2>/dev/null || true
ip addr add ${CLIENT_IP}/24 dev lo 2>/dev/null || true

export LD_PRELOAD=/usr/lib/mmap_nohuge.so

# Clean up any previous shm
rm -f /dev/shm/zf_emu_ilink3_perf 2>/dev/null || true

echo "=== Performance Tests (${ROUNDS} rounds) ===" >&2
echo "  Thresholds: negotiate=${THRESH_NEGOTIATE}ms establish=${THRESH_ESTABLISH}ms sequence_p99=${THRESH_SEQUENCE}ms total=${THRESH_TOTAL}ms" >&2

# Start server
ZF_ATTR="${EMU_ATTR};interface=b2b0" \
    /app/ilink3_server --interface b2b0 --ip ${SERVER_IP} --port ${PORT} &
SERVER_PID=$!
sleep 1

# Run perf client
RC=0
ZF_ATTR="${EMU_ATTR};interface=b2b1" \
    /app/perf_session \
        --interface b2b1 \
        --local-ip ${CLIENT_IP} \
        --host ${SERVER_IP} \
        --port ${PORT} \
        --rounds ${ROUNDS} \
        --output "${RESULTS_DIR}/perf.json" \
        --threshold-negotiate-ms ${THRESH_NEGOTIATE} \
        --threshold-establish-ms ${THRESH_ESTABLISH} \
        --threshold-sequence-ms ${THRESH_SEQUENCE} \
        --threshold-total-ms ${THRESH_TOTAL} \
    || RC=$?

wait ${SERVER_PID} 2>/dev/null || true
rm -f /dev/shm/zf_emu_ilink3_perf 2>/dev/null || true

exit ${RC}
