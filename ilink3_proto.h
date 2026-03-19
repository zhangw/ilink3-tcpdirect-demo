/*
 * ilink3_proto.h — shared transport/protocol utilities
 *
 * Included by both ilink3_client.c and ilink3_server.c.
 * Each TU must define:  static bool g_verbose = false;
 */

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/uio.h>

#include <zf/zf.h>
#include <zf/zf_tcp.h>

#include "ilink3_sbe.h"

/* ── Constants ───────────────────────────────────────────────────────────── */

#define RX_BUF_LEN        4096u
#define TX_BUF_LEN        4096u
#define POLL_TIMEOUT_NS   100000000LL   /* 100 ms                           */
#define HEARTBEAT_SECS    10u
#define KEEPALIVE_MS      (HEARTBEAT_SECS * 1000u)
#define KEEPALIVE_ROUNDS  3
#define CONNECT_TIMEOUT_S 5
#define ILINK3_FTI_PRIMARY 1u

/* Each TU defines: static bool g_verbose = false; */
#define VLOG(...) do { if (g_verbose) fprintf(stderr, __VA_ARGS__); } while(0)

/* ── Session states (superset for client + server) ───────────────────────── */

typedef enum {
    S_IDLE,
    S_LISTENING,           /* server: waiting for connection                */
    S_TCP_CONNECTING,      /* client: TCP handshake in progress             */
    S_TCP_CONNECTED,       /* client: TCP established                       */
    S_TCP_ACCEPTED,        /* server: connection accepted                   */
    S_NEGOTIATING,
    S_NEGOTIATED,
    S_ESTABLISHING,
    S_ACTIVE,
    S_TERMINATING,         /* client: Terminate sent, waiting for close     */
    S_CLOSED,
} session_state_t;

/* ── Common transport state ──────────────────────────────────────────────── */

typedef struct {
    struct zf_stack     *stack;
    struct zf_muxer_set *muxer;
    struct zft          *tcp_sock;
    uint8_t  rx_buf[RX_BUF_LEN];
    size_t   rx_buf_len;
    uint8_t  msg_buf[RX_BUF_LEN];
    uint8_t  tx_buf[TX_BUF_LEN];
} ilink3_transport_t;

/* ── Timestamp ───────────────────────────────────────────────────────────── */

static inline UTCTimestampNanos now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (UTCTimestampNanos)ts.tv_sec * 1000000000ULL +
           (UTCTimestampNanos)ts.tv_nsec;
}

/* ── UUID generation ─────────────────────────────────────────────────────── */
/*
 * CME iLink3 schema: UUID is uInt64, "recommended to use timestamp as number
 * of microseconds since epoch (Jan 1, 1970)".
 */
static inline void gen_uuid(ilink3_uuid_t *u)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    *u = (ilink3_uuid_t)ts.tv_sec * 1000000ULL +
         (ilink3_uuid_t)(ts.tv_nsec / 1000);
}

/* ── ZF stack init ───────────────────────────────────────────────────────── */

static inline int zf_stack_init(ilink3_transport_t *t, const char *iface)
{
    struct zf_attr *attr = NULL;
    int rc;

    rc = zf_init();
    if (rc != 0) { fprintf(stderr, "zf_init() failed: %d\n", rc); return rc; }

    rc = zf_attr_alloc(&attr);
    if (rc != 0) { fprintf(stderr, "zf_attr_alloc() failed: %d\n", rc); return rc; }

    rc = zf_attr_set_str(attr, "interface", iface);
    if (rc != 0) {
        fprintf(stderr, "zf_attr_set_str(interface=%s) failed: %d\n", iface, rc);
        zf_attr_free(attr);
        return rc;
    }

    rc = zf_stack_alloc(attr, &t->stack);
    zf_attr_free(attr);
    if (rc != 0) { fprintf(stderr, "zf_stack_alloc() failed: %d\n", rc); return rc; }

    rc = zf_muxer_alloc(t->stack, &t->muxer);
    if (rc != 0) {
        fprintf(stderr, "zf_muxer_alloc() failed: %d\n", rc);
        zf_stack_free(t->stack);
        t->stack = NULL;
        return rc;
    }

    return 0;
}

/* ── Frame extraction ────────────────────────────────────────────────────── */

static inline ssize_t extract_frame(ilink3_transport_t *t)
{
    if (t->rx_buf_len < sizeof(sofh_t) + sizeof(sbe_header_t))
        return 0;

    const sofh_t *sofh = (const sofh_t *)t->rx_buf;
    if (sofh->encoding_type != ILINK3_ENCODING_TYPE) {
        fprintf(stderr, "[RX] Invalid SOFH encoding type: 0x%04x\n",
                sofh->encoding_type);
        return -EPROTO;
    }
    if (sofh->message_size < sizeof(sofh_t) + sizeof(sbe_header_t)) {
        fprintf(stderr, "[RX] Invalid SOFH message size: %u\n", sofh->message_size);
        return -EPROTO;
    }
    if (sofh->message_size > RX_BUF_LEN) {
        fprintf(stderr, "[RX] Frame too large for buffer: %u\n", sofh->message_size);
        return -EMSGSIZE;
    }
    if (t->rx_buf_len < sofh->message_size)
        return 0;

    uint16_t tmpl = ilink3_parse_template(t->rx_buf, t->rx_buf_len);
    if (tmpl == 0) {
        fprintf(stderr, "[RX] Invalid SBE frame len=%zu\n", t->rx_buf_len);
        return -EPROTO;
    }

    size_t frame_len = sofh->message_size;
    memcpy(t->msg_buf, t->rx_buf, frame_len);
    t->rx_buf_len -= frame_len;
    if (t->rx_buf_len > 0)
        memmove(t->rx_buf, t->rx_buf + frame_len, t->rx_buf_len);

    VLOG("[RX] templateId=%u  len=%zu\n", tmpl, frame_len);
    return (ssize_t)frame_len;
}

/* ── Send a fully-framed iLink3 message ─────────────────────────────────── */

static inline int send_msg(ilink3_transport_t *t, uint16_t tmpl,
                           uint16_t block_len, const void *body, size_t body_len)
{
    size_t frame = ilink3_frame_size(body_len);
    if (frame > TX_BUF_LEN) return -ENOBUFS;
    if (frame > UINT16_MAX) return -EMSGSIZE;

    uint8_t *p = ilink3_write_headers(t->tx_buf, tmpl, block_len, frame);
    memcpy(p, body, body_len);

    struct iovec iov = { .iov_base = t->tx_buf, .iov_len = frame };
    ssize_t sent = zft_send(t->tcp_sock, &iov, 1, 0);
    if (sent < 0) {
        fprintf(stderr, "[TX] zft_send failed: %zd\n", sent);
        return (int)sent;
    }
    VLOG("[TX] templateId=%u  len=%zu\n", tmpl, frame);
    return 0;
}

/* ── Receive one iLink3 message (blocks until data or timeout) ───────────── */

static inline ssize_t recv_msg(ilink3_transport_t *t)
{
    struct epoll_event ev;
    time_t deadline = time(NULL) + 30;

    ssize_t frame = extract_frame(t);
    if (frame != 0)
        return frame;

    while (time(NULL) < deadline) {
        zf_reactor_perform(t->stack);
        zf_muxer_wait(t->muxer, &ev, 1, POLL_TIMEOUT_NS);

        if (t->rx_buf_len == RX_BUF_LEN) {
            fprintf(stderr, "[RX] Stream buffer full without complete frame\n");
            return -ENOBUFS;
        }

        struct iovec iov = {
            .iov_base = t->rx_buf + t->rx_buf_len,
            .iov_len  = RX_BUF_LEN - t->rx_buf_len
        };
        ssize_t n = zft_recv(t->tcp_sock, &iov, 1, 0);
        if (n > 0) {
            t->rx_buf_len += (size_t)n;
            frame = extract_frame(t);
            if (frame != 0)
                return frame;
        } else if (n == 0) {
            fprintf(stderr, "[RX] Connection closed by peer\n");
            return -EPIPE;
        } else if (n != -EAGAIN) {
            fprintf(stderr, "[RX] zft_recv failed: %zd\n", n);
            return n;
        }
    }
    fprintf(stderr, "[RX] Receive timed out\n");
    return -ETIMEDOUT;
}
