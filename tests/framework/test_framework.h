#pragma once
/*
 * test_framework.h — lightweight C test framework with JSON output
 *
 * Usage:
 *   TEST(test_name) { ASSERT_EQ(1, 1); return 0; }
 *   int main() { TEST_INIT("suite_name"); RUN(test_name); TEST_REPORT(); }
 *
 * Output: JSON results to stdout, human-readable progress to stderr.
 * JSON file path configurable via --output <path> CLI arg.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

/* ── Test result storage ─────────────────────────────────────────────────── */

#define MAX_TESTS    256
#define MAX_MSG_LEN  512

typedef struct {
    const char *name;
    int         passed;
    double      duration_ms;
    char        message[MAX_MSG_LEN];
} test_result_t;

typedef struct {
    const char    *suite_name;
    test_result_t  results[MAX_TESTS];
    int            count;
    int            passed;
    int            failed;
    double         total_duration_ms;
    char           output_path[256];
    struct timeval  suite_start;
} test_suite_t;

static test_suite_t _suite;
static char _assert_msg[MAX_MSG_LEN];

/* ── Timing helpers ──────────────────────────────────────────────────────── */

static inline double _elapsed_ms(struct timeval *start)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (now.tv_sec - start->tv_sec) * 1000.0 +
           (now.tv_usec - start->tv_usec) / 1000.0;
}

/* ── Init / CLI parsing ──────────────────────────────────────────────────── */

#define TEST_INIT(name) _test_init(name, argc, argv)

static inline void _test_init(const char *name, int argc, char **argv)
{
    memset(&_suite, 0, sizeof(_suite));
    _suite.suite_name = name;
    snprintf(_suite.output_path, sizeof(_suite.output_path),
             "tests/results/%s.json", name);
    gettimeofday(&_suite.suite_start, NULL);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            strncpy(_suite.output_path, argv[++i],
                    sizeof(_suite.output_path) - 1);
        }
    }
}

/* ── Test definition macro ───────────────────────────────────────────────── */

#define TEST(name) static int name(void)

/* ── Run a test ──────────────────────────────────────────────────────────── */

#define RUN(fn) do { \
    _assert_msg[0] = '\0'; \
    struct timeval _t0; \
    gettimeofday(&_t0, NULL); \
    fprintf(stderr, "  %-50s ", #fn); \
    int _rc = fn(); \
    double _dur = _elapsed_ms(&_t0); \
    test_result_t *_r = &_suite.results[_suite.count++]; \
    _r->name = #fn; \
    _r->passed = (_rc == 0); \
    _r->duration_ms = _dur; \
    strncpy(_r->message, _assert_msg, MAX_MSG_LEN - 1); \
    if (_r->passed) { _suite.passed++; fprintf(stderr, "PASS (%.1fms)\n", _dur); } \
    else { _suite.failed++; fprintf(stderr, "FAIL (%.1fms) %s\n", _dur, _r->message); } \
} while(0)

/* ── Assertions ──────────────────────────────────────────────────────────── */

#define ASSERT_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        snprintf(_assert_msg, MAX_MSG_LEN, "%s:%d: ASSERT_EQ(%s, %s) failed: %lld != %lld", \
                 __FILE__, __LINE__, #a, #b, _a, _b); \
        return 1; \
    } \
} while(0)

#define ASSERT_NEQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a == _b) { \
        snprintf(_assert_msg, MAX_MSG_LEN, "%s:%d: ASSERT_NEQ(%s, %s) failed: both %lld", \
                 __FILE__, __LINE__, #a, #b, _a); \
        return 1; \
    } \
} while(0)

#define ASSERT_OK(expr) do { \
    int _v = (expr); \
    if (_v != 0) { \
        snprintf(_assert_msg, MAX_MSG_LEN, "%s:%d: ASSERT_OK(%s) failed: got %d", \
                 __FILE__, __LINE__, #expr, _v); \
        return 1; \
    } \
} while(0)

#define ASSERT_FAIL(expr) do { \
    int _v = (expr); \
    if (_v == 0) { \
        snprintf(_assert_msg, MAX_MSG_LEN, "%s:%d: ASSERT_FAIL(%s) unexpectedly succeeded", \
                 __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while(0)

#define ASSERT_MEM_EQ(a, b, len) do { \
    if (memcmp((a), (b), (len)) != 0) { \
        snprintf(_assert_msg, MAX_MSG_LEN, "%s:%d: ASSERT_MEM_EQ(%s, %s, %zu) failed", \
                 __FILE__, __LINE__, #a, #b, (size_t)(len)); \
        return 1; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        snprintf(_assert_msg, MAX_MSG_LEN, "%s:%d: ASSERT_STR_EQ(%s, %s) failed: \"%s\" != \"%s\"", \
                 __FILE__, __LINE__, #a, #b, (a), (b)); \
        return 1; \
    } \
} while(0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        snprintf(_assert_msg, MAX_MSG_LEN, "%s:%d: ASSERT_TRUE(%s) failed", \
                 __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while(0)

#define ASSERT_LE(a, b) do { \
    double _a = (double)(a), _b = (double)(b); \
    if (_a > _b) { \
        snprintf(_assert_msg, MAX_MSG_LEN, "%s:%d: ASSERT_LE(%s, %s) failed: %.3f > %.3f", \
                 __FILE__, __LINE__, #a, #b, _a, _b); \
        return 1; \
    } \
} while(0)

/* ── JSON report output ──────────────────────────────────────────────────── */

static inline void _escape_json(const char *in, char *out, size_t out_sz)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j < out_sz - 2; i++) {
        if (in[i] == '"' || in[i] == '\\') { out[j++] = '\\'; }
        if (in[i] == '\n') { out[j++] = '\\'; out[j++] = 'n'; continue; }
        out[j++] = in[i];
    }
    out[j] = '\0';
}

#define TEST_REPORT() _test_report()

static inline int _test_report(void)
{
    _suite.total_duration_ms = _elapsed_ms(&_suite.suite_start);

    /* Get ISO timestamp */
    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", tm);

    fprintf(stderr, "\n%s: %d passed, %d failed, %d total (%.1fms)\n",
            _suite.suite_name, _suite.passed, _suite.failed,
            _suite.count, _suite.total_duration_ms);

    /* Write JSON to file and stdout */
    FILE *f = fopen(_suite.output_path, "w");
    FILE *dests[2] = { stdout, f };
    int ndests = f ? 2 : 1;

#define _TPRINTF(...) do { for (int _d = 0; _d < ndests; _d++) fprintf(dests[_d], __VA_ARGS__); } while(0)

    _TPRINTF("{\n");
    _TPRINTF("  \"suite\": \"%s\",\n", _suite.suite_name);
    _TPRINTF("  \"timestamp\": \"%s\",\n", ts);
    _TPRINTF("  \"duration_ms\": %.1f,\n", _suite.total_duration_ms);
    _TPRINTF("  \"total\": %d,\n", _suite.count);
    _TPRINTF("  \"passed\": %d,\n", _suite.passed);
    _TPRINTF("  \"failed\": %d,\n", _suite.failed);
    _TPRINTF("  \"tests\": [\n");

    for (int i = 0; i < _suite.count; i++) {
        test_result_t *r = &_suite.results[i];
        char esc_msg[MAX_MSG_LEN * 2];
        _escape_json(r->message, esc_msg, sizeof(esc_msg));
        _TPRINTF("    {\"name\": \"%s\", \"passed\": %s, \"duration_ms\": %.3f, \"message\": \"%s\"}%s\n",
                r->name, r->passed ? "true" : "false", r->duration_ms,
                esc_msg, (i < _suite.count - 1) ? "," : "");
    }

    _TPRINTF("  ]\n}\n");

#undef _TPRINTF

    if (f) fclose(f);

    return _suite.failed > 0 ? 1 : 0;
}
