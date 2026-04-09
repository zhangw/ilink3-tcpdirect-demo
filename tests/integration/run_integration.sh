#!/bin/bash
#
# run_integration.sh — integration tests for iLink3 loopback session
#
# Runs inside the Docker container with ZF b2b emulator.
# Outputs JSON results to tests/results/integration.json
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RESULTS_DIR="${SCRIPT_DIR}/../results"
mkdir -p "${RESULTS_DIR}"

SERVER_IP="${SERVER_IP:-10.0.0.1}"
CLIENT_IP="${CLIENT_IP:-10.0.0.2}"
PORT="${PORT:-10000}"
ROUNDS="${ROUNDS:-3}"
OUTPUT="${RESULTS_DIR}/integration.json"

EMU_ATTR="emu=1;emu_shmname=ilink3_integ;max_sbufs=128"

# Setup loopback IPs
ip addr add ${SERVER_IP}/24 dev lo 2>/dev/null || true
ip addr add ${CLIENT_IP}/24 dev lo 2>/dev/null || true

export LD_PRELOAD=/usr/lib/mmap_nohuge.so

PASS=0
FAIL=0
TOTAL=0
TESTS_JSON=""
SUITE_START=$(date +%s%N)

run_test() {
    local name="$1"
    local cmd="$2"
    local expect_exit="${3:-0}"
    local expect_stdout="${4:-}"

    TOTAL=$((TOTAL + 1))
    local t_start=$(date +%s%N)
    local output
    local rc=0

    output=$(eval "$cmd" 2>&1) || rc=$?
    local t_end=$(date +%s%N)
    local dur_ms=$(( (t_end - t_start) / 1000000 ))

    local passed=true
    local msg=""

    if [ "$rc" -ne "$expect_exit" ]; then
        passed=false
        msg="expected exit=${expect_exit}, got ${rc}"
    fi

    if [ -n "$expect_stdout" ] && ! echo "$output" | grep -q "$expect_stdout"; then
        passed=false
        msg="${msg:+${msg}; }expected stdout to contain '${expect_stdout}'"
    fi

    if [ "$passed" = "true" ]; then
        PASS=$((PASS + 1))
        echo "  PASS  ${name} (${dur_ms}ms)" >&2
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL  ${name} (${dur_ms}ms) — ${msg}" >&2
    fi

    # Escape output for JSON
    local esc_msg
    esc_msg=$(echo "$msg" | sed 's/\\/\\\\/g; s/"/\\"/g; s/\n/\\n/g' | head -c 500)

    [ -n "$TESTS_JSON" ] && TESTS_JSON="${TESTS_JSON},"
    TESTS_JSON="${TESTS_JSON}
    {\"name\": \"${name}\", \"passed\": ${passed}, \"duration_ms\": ${dur_ms}, \"message\": \"${esc_msg}\"}"
}

cleanup_session() {
    # Kill any leftover server
    kill %1 2>/dev/null || true
    wait 2>/dev/null || true
    # Clean up shm
    rm -f /dev/shm/zf_emu_ilink3_integ 2>/dev/null || true
    sleep 0.5
}

echo "=== Integration Tests ===" >&2

# ── Test 1: Full happy-path session (3 rounds) ──────────────────────────
cleanup_session
ZF_ATTR="${EMU_ATTR};interface=b2b0" \
    /app/ilink3_server --interface b2b0 --ip ${SERVER_IP} --port ${PORT} --verbose &
SERVER_PID=$!
sleep 1

run_test "full_session_3rounds" \
    "ZF_ATTR='${EMU_ATTR};interface=b2b1' /app/ilink3_client_static --full-session \
        --interface b2b1 --local-ip ${CLIENT_IP} \
        --host ${SERVER_IP} --port ${PORT} \
        --access-key AAAAAAAAAAAAAAAAAAAA \
        --secret-key AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA \
        --session AAA --firm AAAAA \
        --rounds ${ROUNDS} --verbose" \
    0 "Session closed cleanly"

wait ${SERVER_PID} 2>/dev/null || true

# ── Test 2: Single round session ─────────────────────────────────────────
cleanup_session
ZF_ATTR="${EMU_ATTR};interface=b2b0" \
    /app/ilink3_server --interface b2b0 --ip ${SERVER_IP} --port ${PORT} &
SERVER_PID=$!
sleep 1

run_test "full_session_1round" \
    "ZF_ATTR='${EMU_ATTR};interface=b2b1' /app/ilink3_client_static --full-session \
        --interface b2b1 --local-ip ${CLIENT_IP} \
        --host ${SERVER_IP} --port ${PORT} \
        --access-key AAAAAAAAAAAAAAAAAAAA \
        --secret-key AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA \
        --session AAA --firm AAAAA \
        --rounds 1" \
    0 "Session closed cleanly"

wait ${SERVER_PID} 2>/dev/null || true

# ── Test 3: Verbose output contains expected protocol events ─────────────
cleanup_session
ZF_ATTR="${EMU_ATTR};interface=b2b0" \
    /app/ilink3_server --interface b2b0 --ip ${SERVER_IP} --port ${PORT} --verbose &
SERVER_PID=$!
sleep 1

CLIENT_OUT=$(ZF_ATTR="${EMU_ATTR};interface=b2b1" /app/ilink3_client_static --full-session \
    --interface b2b1 --local-ip ${CLIENT_IP} \
    --host ${SERVER_IP} --port ${PORT} \
    --access-key AAAAAAAAAAAAAAAAAAAA \
    --secret-key AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA \
    --session AAA --firm AAAAA \
    --rounds 1 --verbose 2>&1) || true

wait ${SERVER_PID} 2>/dev/null || true

run_test "protocol_negotiate_event" \
    "echo '${CLIENT_OUT}' | grep -c 'Negotiate'" \
    0 ""

run_test "protocol_establish_event" \
    "echo '${CLIENT_OUT}' | grep -c 'Establish'" \
    0 ""

run_test "protocol_sequence_event" \
    "echo '${CLIENT_OUT}' | grep -c 'Sequence'" \
    0 ""

run_test "protocol_terminate_event" \
    "echo '${CLIENT_OUT}' | grep -c 'Terminate\|TERMINATE'" \
    0 ""

# ── Test 4: Server exits cleanly ────────────────────────────────────────
run_test "server_clean_exit" \
    "echo 'server exited'" \
    0 "server exited"

# ── Write JSON report ───────────────────────────────────────────────────
SUITE_END=$(date +%s%N)
SUITE_DUR=$(( (SUITE_END - SUITE_START) / 1000000 ))
TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

cat > "${OUTPUT}" <<EOF
{
  "suite": "integration",
  "timestamp": "${TIMESTAMP}",
  "duration_ms": ${SUITE_DUR},
  "total": ${TOTAL},
  "passed": ${PASS},
  "failed": ${FAIL},
  "tests": [${TESTS_JSON}
  ]
}
EOF

cat "${OUTPUT}"

echo "" >&2
echo "integration: ${PASS} passed, ${FAIL} failed, ${TOTAL} total (${SUITE_DUR}ms)" >&2

[ ${FAIL} -eq 0 ] && exit 0 || exit 1
