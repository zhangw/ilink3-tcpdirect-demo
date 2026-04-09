#!/bin/bash
#
# run_all_tests.sh — master test runner for iLink3 test suite
#
# Runs all test layers in sequence: unit → integration → perf
# Outputs per-suite JSON to tests/results/ and a combined summary.
#
# Environment variables for configuration:
#   SKIP_UNIT=1              skip unit tests
#   SKIP_INTEGRATION=1       skip integration tests
#   SKIP_PERF=1              skip performance tests
#   PERF_ROUNDS=100          number of sequence rounds for perf test
#   THRESH_NEGOTIATE_MS=500  negotiate latency threshold
#   THRESH_ESTABLISH_MS=500  establish latency threshold
#   THRESH_SEQUENCE_MS=500   sequence p99 latency threshold
#   THRESH_TOTAL_MS=120000   total session time threshold
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RESULTS_DIR="${SCRIPT_DIR}/results"
mkdir -p "${RESULTS_DIR}"
# Clean previous results
rm -f "${RESULTS_DIR}"/*.json

TOTAL_PASS=0
TOTAL_FAIL=0
SUITES_RUN=0
FUNC_RC=0
PERF_RC=0

echo "╔══════════════════════════════════════════════════╗"
echo "║         iLink3 Test Suite                        ║"
echo "╚══════════════════════════════════════════════════╝"
echo ""

# ── Unit Tests ───────────────────────────────────────────────────────────
if [ "${SKIP_UNIT}" != "1" ]; then
    echo "━━━ Unit Tests ━━━"

    for test_bin in /app/test_base64url /app/test_hmac_signing /app/test_sbe_framing \
                    /app/test_validation /app/test_frame_extract; do
        if [ -x "$test_bin" ]; then
            name=$(basename "$test_bin")
            echo "  Running ${name}..." >&2
            ${test_bin} --output "${RESULTS_DIR}/${name}.json" || FUNC_RC=1
            echo ""
        fi
    done

    SUITES_RUN=$((SUITES_RUN + 1))
    echo ""
fi

# ── Integration Tests ────────────────────────────────────────────────────
if [ "${SKIP_INTEGRATION}" != "1" ]; then
    echo "━━━ Integration Tests ━━━"
    bash "${SCRIPT_DIR}/integration/run_integration.sh" || FUNC_RC=1
    SUITES_RUN=$((SUITES_RUN + 1))
    echo ""
fi

# ── Performance Tests ────────────────────────────────────────────────────
if [ "${SKIP_PERF}" != "1" ]; then
    echo "━━━ Performance Tests ━━━"
    bash "${SCRIPT_DIR}/perf/run_perf.sh" || PERF_RC=1
    SUITES_RUN=$((SUITES_RUN + 1))
    echo ""
fi

# ── Aggregate counts from JSON results ───────────────────────────────────
for jf in "${RESULTS_DIR}"/test_*.json "${RESULTS_DIR}"/integration.json; do
    [ -f "$jf" ] || continue
    p=$(python3 -c "import json; d=json.load(open('$jf')); print(d.get('passed',0))" 2>/dev/null || echo 0)
    f=$(python3 -c "import json; d=json.load(open('$jf')); print(d.get('failed',0))" 2>/dev/null || echo 0)
    TOTAL_PASS=$((TOTAL_PASS + p))
    TOTAL_FAIL=$((TOTAL_FAIL + f))
done

# Perf threshold failures (separate from functional)
PERF_THRESH_FAIL=0
if [ -f "${RESULTS_DIR}/perf.json" ]; then
    PERF_THRESH_FAIL=$(python3 -c "import json; d=json.load(open('${RESULTS_DIR}/perf.json')); print(d.get('threshold_failures',0))" 2>/dev/null || echo 0)
fi

# ── Combined Summary ─────────────────────────────────────────────────────
TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

# Determine overall result
if [ ${FUNC_RC} -ne 0 ]; then
    OVERALL_RESULT="FAIL"
    OVERALL_RC=1
elif [ ${PERF_RC} -ne 0 ]; then
    OVERALL_RESULT="PASS (perf thresholds exceeded)"
    OVERALL_RC=1
else
    OVERALL_RESULT="PASS"
    OVERALL_RC=0
fi

python3 -c "
import json, glob

results = []
for f in sorted(glob.glob('${RESULTS_DIR}/*.json')):
    if 'summary.json' in f:
        continue
    try:
        with open(f) as fh:
            results.append(json.load(fh))
    except:
        pass

summary = {
    'timestamp': '${TIMESTAMP}',
    'suites_run': ${SUITES_RUN},
    'functional_passed': ${TOTAL_PASS},
    'functional_failed': ${TOTAL_FAIL},
    'perf_threshold_failures': ${PERF_THRESH_FAIL},
    'overall_result': '${OVERALL_RESULT}',
    'suites': results
}

with open('${RESULTS_DIR}/summary.json', 'w') as f:
    json.dump(summary, f, indent=2)

print(json.dumps(summary, indent=2))
" 2>/dev/null || echo '{"error": "summary generation failed"}'

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
printf "║  Functional: %d passed, %d failed                           ║\n" ${TOTAL_PASS} ${TOTAL_FAIL}
if [ ${PERF_THRESH_FAIL} -gt 0 ]; then
    printf "║  Perf: %d threshold(s) exceeded                             ║\n" ${PERF_THRESH_FAIL}
fi
printf "║  RESULT: %-50s║\n" "${OVERALL_RESULT}"
echo "╚══════════════════════════════════════════════════════════════╝"

exit ${OVERALL_RC}
