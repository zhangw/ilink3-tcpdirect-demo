/*
 * perf_session.c — iLink3 session latency measurement
 *
 * Runs a full iLink3 session and measures per-phase latencies.
 * Outputs JSON with min/avg/max/p99/stddev for each phase.
 *
 * Must run inside the Docker container with ZF b2b emulator.
 * This is the CLIENT side — start the server first.
 *
 * Usage:
 *   ./perf_session --interface b2b1 --local-ip 10.0.0.2 \
 *       --host 10.0.0.1 --port 10000 --rounds 100 \
 *       --output tests/results/perf.json \
 *       --threshold-negotiate-ms 100 \
 *       --threshold-establish-ms 100 \
 *       --threshold-sequence-ms 50 \
 *       --threshold-total-ms 60000
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <getopt.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>

#include <openssl/hmac.h>
#include <openssl/sha.h>

static bool g_verbose = false;

#include "../../ilink3_proto.h"
#include "../../ilink3_auth.h"

/* ── Timing ──────────────────────────────────────────────────────────────── */

typedef struct {
    double *samples;
    int     count;
    int     capacity;
} latency_series_t;

static void lat_init(latency_series_t *l, int cap)
{
    l->samples = (double *)calloc((size_t)cap, sizeof(double));
    l->count = 0;
    l->capacity = cap;
}

static void lat_add(latency_series_t *l, double ms)
{
    if (l->count < l->capacity)
        l->samples[l->count++] = ms;
}

static int dbl_cmp(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

typedef struct {
    double min, max, avg, p99, stddev;
} lat_stats_t;

static lat_stats_t lat_compute(latency_series_t *l)
{
    lat_stats_t s = {0};
    if (l->count == 0) return s;

    qsort(l->samples, (size_t)l->count, sizeof(double), dbl_cmp);

    s.min = l->samples[0];
    s.max = l->samples[l->count - 1];

    double sum = 0;
    for (int i = 0; i < l->count; i++) sum += l->samples[i];
    s.avg = sum / l->count;

    int p99_idx = (int)((double)(l->count - 1) * 0.99);
    s.p99 = l->samples[p99_idx];

    double var = 0;
    for (int i = 0; i < l->count; i++) {
        double d = l->samples[i] - s.avg;
        var += d * d;
    }
    s.stddev = sqrt(var / l->count);

    return s;
}

static void lat_free(latency_series_t *l) { free(l->samples); }

static inline double elapsed_ms(struct timespec *start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start->tv_sec) * 1000.0 +
           (now.tv_nsec - start->tv_nsec) / 1e6;
}

/* ── Session (reuse client logic) ────────────────────────────────────────── */

typedef struct {
    ilink3_transport_t   t;
    struct zft_handle   *tcp_handle;
    session_state_t      state;
    ilink3_uuid_t        uuid;
    SeqNum               next_seq_no;
    SeqNum               server_next_seq;
    SeqNum               establish_next_seq_no;
    struct sockaddr_in   server_addr;
    struct sockaddr_in   local_addr;
    uint8_t              secret_key_bin[32];
    char                 interface[32];
} perf_session_t;

static int perf_tcp_connect(perf_session_t *s)
{
    struct zf_attr *attr = NULL;
    zf_attr_alloc(&attr);
    zf_attr_set_str(attr, "interface", s->interface);
    int rc = zft_alloc(s->t.stack, attr, &s->tcp_handle);
    zf_attr_free(attr);
    if (rc != 0) return rc;

    if (s->local_addr.sin_family == AF_INET) {
        rc = zft_addr_bind(s->tcp_handle, (struct sockaddr *)&s->local_addr,
                           sizeof(s->local_addr), 0);
        if (rc != 0) { zft_handle_free(s->tcp_handle); return rc; }
    }

    rc = zft_connect(s->tcp_handle, (struct sockaddr *)&s->server_addr,
                     sizeof(s->server_addr), &s->t.tcp_sock);
    if (rc != 0) { zft_handle_free(s->tcp_handle); return rc; }
    s->tcp_handle = NULL;

    struct epoll_event ev;
    time_t deadline = time(NULL) + CONNECT_TIMEOUT_S;
    while (time(NULL) < deadline) {
        zf_reactor_perform(s->t.stack);
        zf_muxer_wait(s->t.muxer, &ev, 1, POLL_TIMEOUT_NS);
        if (zft_state(s->t.tcp_sock) == TCP_ESTABLISHED) {
            s->state = S_TCP_CONNECTED;
            return 0;
        }
        if (zft_error(s->t.tcp_sock) != 0) return -1;
    }
    return -ETIMEDOUT;
}

static int perf_negotiate(perf_session_t *s)
{
    uint8_t buf[sizeof(negotiate_body_t) + sizeof(uint16_t)];
    memset(buf, 0, sizeof(buf));
    negotiate_body_t *body = (negotiate_body_t *)buf;
    uint64_t ts = now_ns();
    body->uuid = s->uuid;
    body->request_timestamp = ts;
    memset(body->access_key_id, 'A', sizeof(body->access_key_id));
    memcpy(body->session, "AAA", 3);
    memcpy(body->firm, "AAAAA", 5);
    sign_negotiate(s->secret_key_bin, 32, ts, s->uuid, "AAA", "AAAAA",
                   (uint8_t *)body->hmac_signature);
    uint16_t *cred = (uint16_t *)(buf + sizeof(negotiate_body_t));
    *cred = 0;

    int rc = send_msg(&s->t, TMPL_NEGOTIATE, NEGOTIATE_BLOCK_LEN, buf, sizeof(buf));
    if (rc != 0) return rc;

    ssize_t n = recv_msg(&s->t);
    if (n < 0) return (int)n;
    uint16_t tmpl = ilink3_parse_template(s->t.msg_buf, (size_t)n);
    if (tmpl != TMPL_NEGOTIATION_RESP) return -EPROTO;
    s->state = S_NEGOTIATED;
    return 0;
}

static int perf_establish(perf_session_t *s)
{
    uint8_t buf[sizeof(establish_body_t) + sizeof(uint16_t)];
    memset(buf, 0, sizeof(buf));
    establish_body_t *body = (establish_body_t *)buf;
    uint64_t ts = now_ns();
    body->uuid = s->uuid;
    body->request_timestamp = ts;
    memset(body->access_key_id, 'A', sizeof(body->access_key_id));
    memcpy(body->session, "AAA", 3);
    memcpy(body->firm, "AAAAA", 5);
    strncpy(body->trading_system_name, "perf", sizeof(body->trading_system_name));
    strncpy(body->trading_system_version, "1.0", sizeof(body->trading_system_version));
    strncpy(body->trading_system_vendor, "test", sizeof(body->trading_system_vendor));
    body->next_seq_no = s->establish_next_seq_no;
    body->keep_alive_interval = KEEPALIVE_MS;
    sign_establish(s->secret_key_bin, 32, ts, s->uuid, "AAA", "AAAAA",
                   "perf", "1.0", "test", s->establish_next_seq_no,
                   KEEPALIVE_MS, (uint8_t *)body->hmac_signature);
    uint16_t *cred = (uint16_t *)(buf + sizeof(establish_body_t));
    *cred = 0;

    int rc = send_msg(&s->t, TMPL_ESTABLISH, ESTABLISH_BLOCK_LEN, buf, sizeof(buf));
    if (rc != 0) return rc;

    ssize_t n = recv_msg(&s->t);
    if (n < 0) return (int)n;
    uint16_t tmpl = ilink3_parse_template(s->t.msg_buf, (size_t)n);
    if (tmpl != TMPL_ESTABLISHMENT_ACK) return -EPROTO;
    s->state = S_ACTIVE;
    return 0;
}

static int perf_sequence(perf_session_t *s)
{
    sequence_body_t body;
    body.uuid = s->uuid;
    body.next_seq_no = s->next_seq_no++;
    body.fault_tolerance_ind = ILINK3_FTI_PRIMARY;
    body.keep_alive_interval_lapsed = 0;

    int rc = send_msg(&s->t, TMPL_SEQUENCE, SEQUENCE_BLOCK_LEN, &body, sizeof(body));
    if (rc != 0) return rc;

    ssize_t n = recv_msg(&s->t);
    if (n < 0) return (int)n;
    return 0;
}

static int perf_terminate(perf_session_t *s)
{
    terminate_body_t body;
    memset(&body, 0, sizeof(body));
    strncpy(body.reason, "perf_done", sizeof(body.reason));
    body.uuid = s->uuid;
    body.request_timestamp = now_ns();
    body.split_msg = U8_NULL;
    return send_msg(&s->t, TMPL_TERMINATE, TERMINATE_BLOCK_LEN, &body, sizeof(body));
}

static void perf_cleanup(perf_session_t *s)
{
    if (s->t.tcp_sock) { zft_free(s->t.tcp_sock); s->t.tcp_sock = NULL; }
    if (s->tcp_handle) { zft_handle_free(s->tcp_handle); s->tcp_handle = NULL; }
    if (s->t.muxer) { zf_muxer_free(s->t.muxer); s->t.muxer = NULL; }
    if (s->t.stack) { zf_stack_free(s->t.stack); s->t.stack = NULL; }
    zf_deinit();
}

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    char host[64] = "10.0.0.1";
    int  port = 10000;
    char iface[32] = "b2b1";
    char local_ip[64] = "10.0.0.2";
    int  rounds = 100;
    char output[256] = "tests/results/perf.json";

    /* Configurable thresholds (ms) — 0 means no threshold check */
    double thresh_negotiate_ms = 0;
    double thresh_establish_ms = 0;
    double thresh_sequence_ms  = 0;
    double thresh_total_ms     = 0;

    static struct option opts[] = {
        {"host",                     required_argument, 0, 'h'},
        {"port",                     required_argument, 0, 'p'},
        {"interface",                required_argument, 0, 'i'},
        {"local-ip",                 required_argument, 0, 'L'},
        {"rounds",                   required_argument, 0, 'r'},
        {"output",                   required_argument, 0, 'o'},
        {"threshold-negotiate-ms",   required_argument, 0, 'N'},
        {"threshold-establish-ms",   required_argument, 0, 'E'},
        {"threshold-sequence-ms",    required_argument, 0, 'S'},
        {"threshold-total-ms",       required_argument, 0, 'T'},
        {"verbose",                  no_argument,       0, 'v'},
        {0, 0, 0, 0}
    };

    int c, idx;
    while ((c = getopt_long(argc, argv, "", opts, &idx)) != -1) {
        switch (c) {
        case 'h': strncpy(host, optarg, sizeof(host) - 1); break;
        case 'p': port = atoi(optarg); break;
        case 'i': strncpy(iface, optarg, sizeof(iface) - 1); break;
        case 'L': strncpy(local_ip, optarg, sizeof(local_ip) - 1); break;
        case 'r': rounds = atoi(optarg); break;
        case 'o': strncpy(output, optarg, sizeof(output) - 1); break;
        case 'N': thresh_negotiate_ms = atof(optarg); break;
        case 'E': thresh_establish_ms = atof(optarg); break;
        case 'S': thresh_sequence_ms  = atof(optarg); break;
        case 'T': thresh_total_ms     = atof(optarg); break;
        case 'v': g_verbose = true; break;
        default: return 1;
        }
    }

    perf_session_t s;
    memset(&s, 0, sizeof(s));
    s.next_seq_no = 1;
    s.establish_next_seq_no = 1;
    strncpy(s.interface, iface, sizeof(s.interface) - 1);
    memset(s.secret_key_bin, 0xAA, 32);

    s.server_addr.sin_family = AF_INET;
    s.server_addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, host, &s.server_addr.sin_addr);

    if (local_ip[0]) {
        s.local_addr.sin_family = AF_INET;
        inet_pton(AF_INET, local_ip, &s.local_addr.sin_addr);
    }

    gen_uuid(&s.uuid);

    int rc = zf_stack_init(&s.t, s.interface);
    if (rc != 0) { fprintf(stderr, "ZF init failed\n"); return 1; }

    struct timespec ts_total;
    clock_gettime(CLOCK_MONOTONIC, &ts_total);

    /* TCP connect */
    rc = perf_tcp_connect(&s);
    if (rc != 0) { fprintf(stderr, "TCP connect failed\n"); perf_cleanup(&s); return 1; }

    /* Negotiate */
    struct timespec ts_phase;
    clock_gettime(CLOCK_MONOTONIC, &ts_phase);
    rc = perf_negotiate(&s);
    double negotiate_ms = elapsed_ms(&ts_phase);
    if (rc != 0) { fprintf(stderr, "Negotiate failed\n"); perf_cleanup(&s); return 1; }

    /* Establish */
    clock_gettime(CLOCK_MONOTONIC, &ts_phase);
    rc = perf_establish(&s);
    double establish_ms = elapsed_ms(&ts_phase);
    if (rc != 0) { fprintf(stderr, "Establish failed\n"); perf_cleanup(&s); return 1; }

    /* Sequence rounds */
    latency_series_t seq_lat;
    lat_init(&seq_lat, rounds);

    for (int i = 0; i < rounds; i++) {
        clock_gettime(CLOCK_MONOTONIC, &ts_phase);
        rc = perf_sequence(&s);
        double ms = elapsed_ms(&ts_phase);
        if (rc != 0) {
            fprintf(stderr, "Sequence round %d failed\n", i);
            break;
        }
        lat_add(&seq_lat, ms);
    }

    /* Terminate */
    perf_terminate(&s);
    double total_ms = elapsed_ms(&ts_total);

    perf_cleanup(&s);

    /* Compute stats */
    lat_stats_t seq_stats = lat_compute(&seq_lat);
    lat_free(&seq_lat);

    /* Threshold checks */
    int failures = 0;
    char thresh_results[2048] = "";
    int tpos = 0;

    if (thresh_negotiate_ms > 0 && negotiate_ms > thresh_negotiate_ms) {
        tpos += snprintf(thresh_results + tpos, sizeof(thresh_results) - (size_t)tpos,
            "    {\"name\": \"negotiate_threshold\", \"passed\": false, \"limit_ms\": %.1f, \"actual_ms\": %.3f},\n",
            thresh_negotiate_ms, negotiate_ms);
        failures++;
    } else if (thresh_negotiate_ms > 0) {
        tpos += snprintf(thresh_results + tpos, sizeof(thresh_results) - (size_t)tpos,
            "    {\"name\": \"negotiate_threshold\", \"passed\": true, \"limit_ms\": %.1f, \"actual_ms\": %.3f},\n",
            thresh_negotiate_ms, negotiate_ms);
    }

    if (thresh_establish_ms > 0 && establish_ms > thresh_establish_ms) {
        tpos += snprintf(thresh_results + tpos, sizeof(thresh_results) - (size_t)tpos,
            "    {\"name\": \"establish_threshold\", \"passed\": false, \"limit_ms\": %.1f, \"actual_ms\": %.3f},\n",
            thresh_establish_ms, establish_ms);
        failures++;
    } else if (thresh_establish_ms > 0) {
        tpos += snprintf(thresh_results + tpos, sizeof(thresh_results) - (size_t)tpos,
            "    {\"name\": \"establish_threshold\", \"passed\": true, \"limit_ms\": %.1f, \"actual_ms\": %.3f},\n",
            thresh_establish_ms, establish_ms);
    }

    if (thresh_sequence_ms > 0 && seq_stats.p99 > thresh_sequence_ms) {
        tpos += snprintf(thresh_results + tpos, sizeof(thresh_results) - (size_t)tpos,
            "    {\"name\": \"sequence_p99_threshold\", \"passed\": false, \"limit_ms\": %.1f, \"actual_p99_ms\": %.3f},\n",
            thresh_sequence_ms, seq_stats.p99);
        failures++;
    } else if (thresh_sequence_ms > 0) {
        tpos += snprintf(thresh_results + tpos, sizeof(thresh_results) - (size_t)tpos,
            "    {\"name\": \"sequence_p99_threshold\", \"passed\": true, \"limit_ms\": %.1f, \"actual_p99_ms\": %.3f},\n",
            thresh_sequence_ms, seq_stats.p99);
    }

    if (thresh_total_ms > 0 && total_ms > thresh_total_ms) {
        tpos += snprintf(thresh_results + tpos, sizeof(thresh_results) - (size_t)tpos,
            "    {\"name\": \"total_threshold\", \"passed\": false, \"limit_ms\": %.1f, \"actual_ms\": %.3f}\n",
            thresh_total_ms, total_ms);
        failures++;
    } else if (thresh_total_ms > 0) {
        tpos += snprintf(thresh_results + tpos, sizeof(thresh_results) - (size_t)tpos,
            "    {\"name\": \"total_threshold\", \"passed\": true, \"limit_ms\": %.1f, \"actual_ms\": %.3f}\n",
            thresh_total_ms, total_ms);
    }

    /* Remove trailing comma if present */
    if (tpos > 2 && thresh_results[tpos - 2] == ',') {
        thresh_results[tpos - 2] = '\n';
        thresh_results[tpos - 1] = '\0';
    }

    /* Write JSON */
    FILE *f = fopen(output, "w");
    if (!f) f = stdout;

    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm);

    fprintf(f, "{\n");
    fprintf(f, "  \"suite\": \"perf\",\n");
    fprintf(f, "  \"timestamp\": \"%s\",\n", timestamp);
    fprintf(f, "  \"rounds\": %d,\n", rounds);
    fprintf(f, "  \"negotiate_ms\": %.3f,\n", negotiate_ms);
    fprintf(f, "  \"establish_ms\": %.3f,\n", establish_ms);
    fprintf(f, "  \"total_ms\": %.3f,\n", total_ms);
    fprintf(f, "  \"sequence\": {\n");
    fprintf(f, "    \"count\": %d,\n", seq_stats.min > 0 ? rounds : 0);
    fprintf(f, "    \"min_ms\": %.3f,\n", seq_stats.min);
    fprintf(f, "    \"avg_ms\": %.3f,\n", seq_stats.avg);
    fprintf(f, "    \"max_ms\": %.3f,\n", seq_stats.max);
    fprintf(f, "    \"p99_ms\": %.3f,\n", seq_stats.p99);
    fprintf(f, "    \"stddev_ms\": %.3f\n", seq_stats.stddev);
    fprintf(f, "  },\n");
    fprintf(f, "  \"thresholds\": [\n%s  ],\n", thresh_results);
    fprintf(f, "  \"threshold_failures\": %d\n", failures);
    fprintf(f, "}\n");

    if (f != stdout) fclose(f);

    /* Print to stdout too */
    if (f != stdout) {
        FILE *f2 = fopen(output, "r");
        if (f2) {
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), f2)) > 0)
                fwrite(buf, 1, n, stdout);
            fclose(f2);
        }
    }

    fprintf(stderr, "\n[PERF] negotiate=%.3fms establish=%.3fms seq(avg=%.3f p99=%.3f) total=%.3fms\n",
            negotiate_ms, establish_ms, seq_stats.avg, seq_stats.p99, total_ms);
    if (failures > 0)
        fprintf(stderr, "[PERF] %d threshold(s) EXCEEDED\n", failures);

    return failures > 0 ? 1 : 0;
}
