/*
 * ilink3_demo.c
 *
 * Minimal CME iLink3 v9 demo using TCPDirect (libonload_zf.so).
 *
 * Protocol flow:
 *   TCP connect → Negotiate(500) → NegotiationResponse(501)
 *               → Establish(503) → EstablishmentAck(504)
 *               → Sequence(506) keepalives (N rounds)
 *               → Terminate(507) → TCP close
 *
 * Build:
 *   make          (auto-detects macOS → zig cc cross-compile, Linux → gcc)
 *
 * Run:
 *   ./ilink3_demo \
 *       --interface eth1 \
 *       --host 10.0.0.1 --port 10000
 *
 *   Full session:
 *   ./ilink3_demo --full-session \
 *       --interface eth1 \
 *       --host 10.0.0.1 --port 10000 \
 *       --access-key <20-char key> \
 *       --secret-key <43-char base64url secret> \
 *       --session <session-id> --firm <firm-id>
 *
 * Notes:
 *   - The HMAC-SHA256 signature is computed at runtime over a canonical
 *     string ("CME-1-SHA-256" scheme):
 *       Negotiate:  str(ts) + "\n" + str(uuid) + "\n" + session + "\n" + firm
 *       Establish:  above + "\n" + ts_name + "\n" + ts_ver + "\n" + ts_vendor
 *                   + "\n" + str(next_seq_no) + "\n" + str(keep_alive_interval)
 *   - Sequence numbers start at 1 for a brand-new session.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <netinet/tcp.h>

/* TCPDirect headers – provided by Onload installation */
#include <zf/zf.h>
#include <zf/zf_tcp.h>

#include <openssl/hmac.h>
#include <openssl/sha.h>

#include "ilink3_sbe.h"

/* ── Compile-time tunables ───────────────────────────────────────────────── */

#define RX_BUF_LEN        4096u
#define TX_BUF_LEN        4096u
#define POLL_TIMEOUT_NS   100000000LL   /* 100 ms                           */
#define HEARTBEAT_SECS    10u           /* heartbeat interval in seconds        */
#define KEEPALIVE_MS      (HEARTBEAT_SECS * 1000u) /* KeepAliveInterval in ms  */
#define KEEPALIVE_ROUNDS  3             /* how many Sequence rounds to send */
#define CONNECT_TIMEOUT_S 5
#define ILINK3_FTI_PRIMARY 1u

static bool g_verbose = false;

#define VLOG(...) \
    do { \
        if (g_verbose) \
            fprintf(stderr, __VA_ARGS__); \
    } while (0)

/* ── Base64URL decode ────────────────────────────────────────────────────── */
/* Decodes Base64URL (RFC 4648 §5) without padding.                           */

static int base64url_decode(const char *in, uint8_t *out, size_t *out_len)
{
    /* A-Z=0-25, a-z=26-51, 0-9=52-61, -=62, _=63 */
    static const int8_t T[128] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 0x00-0x0f */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 0x10-0x1f */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,  /* 0x20-0x2f  - */
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,  /* 0x30-0x3f  0-9 */
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14, /* 0x40-0x4f  A-O */
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,63,  /* 0x50-0x5f  P-Z _ */
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,  /* 0x60-0x6f  a-o */
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,  /* 0x70-0x7f  p-z */
    };
    size_t j = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; in[i] != '\0' && in[i] != '='; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c >= 128 || T[c] < 0) return -1;
        acc  = (acc << 6) | (uint32_t)T[c];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[j++] = (uint8_t)((acc >> bits) & 0xff);
        }
    }
    *out_len = j;
    return 0;
}

/* ── HMAC signing for iLink3 ─────────────────────────────────────────────── */
/*
 * CME "CME-1-SHA-256" canonical strings (per CME iLink3 Binary OE spec):
 *
 * Negotiate (500):
 *   str(RequestTimestamp) + "\n" + str(UUID) + "\n" + Session + "\n" + Firm
 *
 * Establish (503):
 *   str(RequestTimestamp) + "\n" + str(UUID) + "\n" + Session + "\n" + Firm
 *   + "\n" + TradingSystemName + "\n" + TradingSystemVersion
 *   + "\n" + TradingSystemVendor + "\n" + str(NextSeqNo)
 *   + "\n" + str(KeepAliveInterval)
 *
 * Integers are printed as unsigned decimal. UUID is the raw uint64 in decimal.
 * Session and Firm are the exact string values (no padding).
 */
static int sign_negotiate(const uint8_t *secret, size_t secret_len,
                          uint64_t timestamp, uint64_t uuid,
                          const char *session, const char *firm,
                          uint8_t out[SHA256_DIGEST_LENGTH])
{
    char canonical[256];
    int n = snprintf(canonical, sizeof(canonical),
                     "%llu\n%llu\n%s\n%s",
                     (unsigned long long)timestamp,
                     (unsigned long long)uuid,
                     session, firm);
    if (n < 0 || n >= (int)sizeof(canonical)) return -1;
    VLOG("[HMAC] negotiate canonical: %s\n", canonical);
    unsigned int md_len = SHA256_DIGEST_LENGTH;
    return HMAC(EVP_sha256(), secret, (int)secret_len,
                (const unsigned char *)canonical, (size_t)n,
                out, &md_len) ? 0 : -1;
}

static int sign_establish(const uint8_t *secret, size_t secret_len,
                          uint64_t timestamp, uint64_t uuid,
                          const char *session, const char *firm,
                          const char *ts_name, const char *ts_version,
                          const char *ts_vendor, uint32_t next_seq_no,
                          uint16_t keep_alive_interval,
                          uint8_t out[SHA256_DIGEST_LENGTH])
{
    char canonical[512];
    int n = snprintf(canonical, sizeof(canonical),
                     "%llu\n%llu\n%s\n%s\n%s\n%s\n%s\n%u\n%u",
                     (unsigned long long)timestamp,
                     (unsigned long long)uuid,
                     session, firm,
                     ts_name, ts_version, ts_vendor,
                     (unsigned)next_seq_no, (unsigned)keep_alive_interval);
    if (n < 0 || n >= (int)sizeof(canonical)) return -1;
    VLOG("[HMAC] establish canonical: %s\n", canonical);
    unsigned int md_len = SHA256_DIGEST_LENGTH;
    return HMAC(EVP_sha256(), secret, (int)secret_len,
                (const unsigned char *)canonical, (size_t)n,
                out, &md_len) ? 0 : -1;
}

/* ── Session state ───────────────────────────────────────────────────────── */

typedef enum {
    S_IDLE,
    S_TCP_CONNECTING,
    S_TCP_CONNECTED,
    S_NEGOTIATING,
    S_NEGOTIATED,
    S_ESTABLISHING,
    S_ACTIVE,
    S_TERMINATING,
    S_CLOSED,
} session_state_t;

typedef struct {
    /* TCPDirect resources */
    struct zf_stack      *stack;
    struct zf_muxer_set  *muxer;
    struct zft_handle    *tcp_handle;   /* pre-connect handle               */
    struct zft           *tcp_sock;     /* post-connect socket              */

    /* Protocol state */
    session_state_t  state;
    ilink3_uuid_t    uuid;             /* our session UUID (64-bit)         */
    SeqNum           next_seq_no;      /* next seq# we will send            */
    SeqNum           server_next_seq;  /* server's next expected seq#       */
    SeqNum           establish_next_seq_no; /* sent in Establish.NextSeqNo  */
    uint16_t         heartbt_int;

    /* Config */
    struct sockaddr_in  server_addr;
    char    access_key[21];            /* 20 chars + NUL                   */
    uint8_t secret_key_bin[32];        /* binary secret key (from Base64URL) */
    char    session_id[4];             /* CME Session field, 3 chars + NUL */
    char    firm_id[6];                /* CME Firm field, 5 chars + NUL    */
    char    interface[32];
    char    app_name[21];
    char    app_ver[11];
    char    app_vendor[11];

    /* I/O buffers */
    uint8_t  rx_buf[RX_BUF_LEN];
    size_t   rx_buf_len;
    uint8_t  msg_buf[RX_BUF_LEN];
    uint8_t  tx_buf[TX_BUF_LEN];
} demo_session_t;

/* ── Utility: nanosecond timestamp ──────────────────────────────────────── */

static UTCTimestampNanos now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (UTCTimestampNanos)ts.tv_sec * 1000000000ULL +
           (UTCTimestampNanos)ts.tv_nsec;
}

static int validate_fixed_fields(const demo_session_t *s)
{
    size_t access_key_len = strlen(s->access_key);
    size_t session_len = strlen(s->session_id);
    size_t firm_len = strlen(s->firm_id);
    size_t interface_len = strlen(s->interface);
    size_t app_name_len = strlen(s->app_name);
    size_t app_ver_len = strlen(s->app_ver);
    size_t app_vendor_len = strlen(s->app_vendor);

    if (access_key_len != sizeof(((negotiate_body_t *)0)->access_key_id)) {
        fprintf(stderr, "Error: --access-key must be exactly %zu chars\n",
                sizeof(((negotiate_body_t *)0)->access_key_id));
        return -EINVAL;
    }
    if (session_len == 0 || session_len > sizeof(((negotiate_body_t *)0)->session)) {
        fprintf(stderr, "Error: --session must be 1-%zu chars\n",
                sizeof(((negotiate_body_t *)0)->session));
        return -EINVAL;
    }
    if (firm_len == 0 || firm_len > sizeof(((negotiate_body_t *)0)->firm)) {
        fprintf(stderr, "Error: --firm must be 1-%zu chars\n",
                sizeof(((negotiate_body_t *)0)->firm));
        return -EINVAL;
    }
    if (interface_len == 0 || interface_len >= sizeof(s->interface)) {
        fprintf(stderr, "Error: --interface must be 1-%zu chars\n",
                sizeof(s->interface) - 1);
        return -EINVAL;
    }
    if (app_name_len == 0 || app_name_len > sizeof(((establish_body_t *)0)->trading_system_name)) {
        fprintf(stderr, "Error: --app-name must be 1-%zu chars\n",
                sizeof(((establish_body_t *)0)->trading_system_name));
        return -EINVAL;
    }
    if (app_ver_len == 0 || app_ver_len > sizeof(((establish_body_t *)0)->trading_system_version)) {
        fprintf(stderr, "Error: --app-ver must be 1-%zu chars\n",
                sizeof(((establish_body_t *)0)->trading_system_version));
        return -EINVAL;
    }
    if (app_vendor_len == 0 ||
        app_vendor_len > sizeof(((establish_body_t *)0)->trading_system_vendor)) {
        fprintf(stderr, "Error: --app-vendor must be 1-%zu chars\n",
                sizeof(((establish_body_t *)0)->trading_system_vendor));
        return -EINVAL;
    }
    return 0;
}

static ssize_t extract_frame(demo_session_t *s)
{
    if (s->rx_buf_len < sizeof(sofh_t) + sizeof(sbe_header_t))
        return 0;

    const sofh_t *sofh = (const sofh_t *)s->rx_buf;
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
    if (s->rx_buf_len < sofh->message_size)
        return 0;

    uint16_t tmpl = ilink3_parse_template(s->rx_buf, s->rx_buf_len);
    if (tmpl == 0) {
        fprintf(stderr, "[RX] Invalid SBE frame len=%zu\n", s->rx_buf_len);
        return -EPROTO;
    }

    size_t frame_len = sofh->message_size;
    memcpy(s->msg_buf, s->rx_buf, frame_len);
    s->rx_buf_len -= frame_len;
    if (s->rx_buf_len > 0)
        memmove(s->rx_buf, s->rx_buf + frame_len, s->rx_buf_len);

    VLOG("[RX] templateId=%u  len=%zu\n", tmpl, frame_len);
    return (ssize_t)frame_len;
}

/* ── Utility: generate UUID as microseconds since epoch ─────────────────── */
/*
 * CME iLink3 schema: UUID is uInt64, "recommended to use timestamp as number
 * of microseconds since epoch (Jan 1, 1970)".  Using the current time in
 * microseconds ensures uniqueness across sessions and satisfies CME validation.
 */
static void gen_uuid(ilink3_uuid_t *u)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    *u = (ilink3_uuid_t)ts.tv_sec * 1000000ULL +
         (ilink3_uuid_t)(ts.tv_nsec / 1000);
}

/* ── TCPDirect init ──────────────────────────────────────────────────────── */

static int zf_init_session(demo_session_t *s)
{
    struct zf_attr *attr = NULL;
    int rc;

    rc = zf_init();
    if (rc != 0) {
        fprintf(stderr, "zf_init() failed: %d\n", rc);
        return rc;
    }

    rc = zf_attr_alloc(&attr);
    if (rc != 0) {
        fprintf(stderr, "zf_attr_alloc() failed: %d\n", rc);
        return rc;
    }

    rc = zf_attr_set_str(attr, "interface", s->interface);
    if (rc != 0) {
        fprintf(stderr, "zf_attr_set_str(interface=%s) failed: %d\n",
                s->interface, rc);
        zf_attr_free(attr);
        return rc;
    }

    rc = zf_stack_alloc(attr, &s->stack);
    zf_attr_free(attr);
    if (rc != 0) {
        fprintf(stderr, "zf_stack_alloc() failed: %d\n", rc);
        return rc;
    }

    rc = zf_muxer_alloc(s->stack, &s->muxer);
    if (rc != 0) {
        fprintf(stderr, "zf_muxer_alloc() failed: %d\n", rc);
        zf_stack_free(s->stack);
        s->stack = NULL;
        return rc;
    }

    return 0;
}

/* ── TCP connect ─────────────────────────────────────────────────────────── */

static int tcp_connect(demo_session_t *s)
{
    struct zf_attr *attr = NULL;
    int rc;

    rc = zf_attr_alloc(&attr);
    if (rc != 0) return rc;

    rc = zf_attr_set_str(attr, "interface", s->interface);
    if (rc != 0) {
        fprintf(stderr, "zf_attr_set_str(interface=%s) failed: %d\n",
                s->interface, rc);
        zf_attr_free(attr);
        return rc;
    }

    rc = zft_alloc(s->stack, attr, &s->tcp_handle);
    zf_attr_free(attr);
    if (rc != 0) {
        fprintf(stderr, "zft_alloc() failed: %d\n", rc);
        return rc;
    }

    s->state = S_TCP_CONNECTING;

    rc = zft_connect(s->tcp_handle,
                     (struct sockaddr *)&s->server_addr,
                     sizeof(s->server_addr),
                     &s->tcp_sock);
    if (rc != 0) {
        fprintf(stderr, "zft_connect() failed: %d\n", rc);
        zft_handle_free(s->tcp_handle);
        s->tcp_handle = NULL;
        return rc;
    }
    s->tcp_handle = NULL;   /* consumed by zft_connect */

    /* Poll until connected. Do not probe with zft_send(): TCPDirect does not
     * support zero-length sends, and sending before establishment is outside
     * the API contract. */
    struct epoll_event ev;
    time_t deadline = time(NULL) + CONNECT_TIMEOUT_S;
    while (time(NULL) < deadline) {
        zf_reactor_perform(s->stack);
        (void)zf_muxer_wait(s->muxer, &ev, 1, POLL_TIMEOUT_NS);

        int err = zft_error(s->tcp_sock);
        if (err != 0) {
            fprintf(stderr, "[TCP] Connect failed: %s (%d)\n",
                    strerror(err), err);
            return -err;
        }

        if (zft_state(s->tcp_sock) == TCP_ESTABLISHED) {
            s->state = S_TCP_CONNECTED;
            printf("[TCP] Connected to %s:%d\n",
                   inet_ntoa(s->server_addr.sin_addr),
                   ntohs(s->server_addr.sin_port));
            return 0;
        }
    }

    fprintf(stderr, "[TCP] Connect timed out\n");
    return -ETIMEDOUT;
}

/* ── Send a fully-framed iLink3 message ─────────────────────────────────── */

static int send_msg(demo_session_t *s, uint16_t tmpl,
                    uint16_t block_len, const void *body, size_t body_len)
{
    size_t frame = ilink3_frame_size(body_len);
    if (frame > TX_BUF_LEN) return -ENOBUFS;
    if (frame > UINT16_MAX) return -EMSGSIZE;

    uint8_t *p = ilink3_write_headers(s->tx_buf, tmpl, block_len, frame);
    memcpy(p, body, body_len);

    struct iovec iov = { .iov_base = s->tx_buf, .iov_len = frame };
    ssize_t sent = zft_send(s->tcp_sock, &iov, 1, 0);
    if (sent < 0) {
        fprintf(stderr, "[TX] zft_send failed: %zd\n", sent);
        return (int)sent;
    }
    VLOG("[TX] templateId=%u  len=%zu  nextSeqNo=%u\n",
         tmpl, frame, s->next_seq_no);
    return 0;
}

/* ── Receive one iLink3 message (blocks until data or timeout) ───────────── */

static ssize_t recv_msg(demo_session_t *s)
{
    struct epoll_event ev;
    time_t deadline = time(NULL) + 30;

    ssize_t frame = extract_frame(s);
    if (frame != 0)
        return frame;

    while (time(NULL) < deadline) {
        zf_reactor_perform(s->stack);
        zf_muxer_wait(s->muxer, &ev, 1, POLL_TIMEOUT_NS);

        if (s->rx_buf_len == RX_BUF_LEN) {
            fprintf(stderr, "[RX] Stream buffer full without complete frame\n");
            return -ENOBUFS;
        }

        struct iovec iov = {
            .iov_base = s->rx_buf + s->rx_buf_len,
            .iov_len = RX_BUF_LEN - s->rx_buf_len
        };
        ssize_t n = zft_recv(s->tcp_sock, &iov, 1, 0);
        if (n > 0) {
            s->rx_buf_len += (size_t)n;
            frame = extract_frame(s);
            if (frame != 0)
                return frame;
        }
        else if (n == 0) {
            fprintf(stderr, "[RX] Connection closed by peer\n");
            return -EPIPE;
        }
        else if (n != -EAGAIN) {
            fprintf(stderr, "[RX] zft_recv failed: %zd\n", n);
            return n;
        }
    }
    fprintf(stderr, "[RX] Receive timed out\n");
    return -ETIMEDOUT;
}

/* ── Phase 1: Negotiate (500) ────────────────────────────────────────────── */

static int do_negotiate(demo_session_t *s)
{
    uint8_t buf[sizeof(negotiate_body_t) + sizeof(uint16_t)];
    memset(buf, 0, sizeof(buf));
    negotiate_body_t *body = (negotiate_body_t *)buf;

    uint64_t ts = now_ns();
    body->uuid              = s->uuid;
    body->request_timestamp = ts;

    memcpy(body->access_key_id, s->access_key, sizeof(body->access_key_id));
    memcpy(body->session, s->session_id, sizeof(body->session));
    memcpy(body->firm,    s->firm_id,    sizeof(body->firm));

    if (sign_negotiate(s->secret_key_bin, sizeof(s->secret_key_bin),
                       ts, s->uuid, s->session_id, s->firm_id,
                       (uint8_t *)body->hmac_signature) != 0) {
        fprintf(stderr, "[NEGOTIATE] HMAC signing failed\n");
        return -EINVAL;
    }

    /* Credentials DATA trailer: zero-length */
    uint16_t *cred_len = (uint16_t *)(buf + sizeof(negotiate_body_t));
    *cred_len = 0;

    printf("[NEGOTIATE] Sending Negotiate (tmpl=500)  uuid=%016llx\n",
           (unsigned long long)s->uuid);

    int rc = send_msg(s, TMPL_NEGOTIATE, NEGOTIATE_BLOCK_LEN,
                      buf, sizeof(buf));
    if (rc != 0) return rc;

    s->state = S_NEGOTIATING;

    /* Receive NegotiationResponse, NegotiationReject, or Terminate */
    ssize_t n = recv_msg(s);
    if (n < 0) return (int)n;

    uint16_t tmpl = ilink3_parse_template(s->msg_buf, (size_t)n);

    if (tmpl == TMPL_NEGOTIATION_RESP) {
        negotiation_resp_body_t *resp =
            (negotiation_resp_body_t *)(s->msg_buf + sizeof(sofh_t) + sizeof(sbe_header_t));
        /* 501 has no NextSeqNo; PreviousSeqNo/PreviousUUID carry prior session info */
        s->server_next_seq = (resp->prev_seq_no == 0) ? 1u : (resp->prev_seq_no + 1u);
        s->establish_next_seq_no = s->server_next_seq;
        printf("[NEGOTIATE] NegotiationResponse received\n");
        VLOG("[NEGOTIATE] prev_seq_no=%u  next_in_seq=%u  fault_tol=%u\n",
             resp->prev_seq_no, s->establish_next_seq_no, resp->fault_tolerance_ind);
        s->heartbt_int = HEARTBEAT_SECS;
        s->state = S_NEGOTIATED;
        return 0;
    } else if (tmpl == TMPL_NEGOTIATION_REJECT) {
        negotiation_reject_body_t *rej =
            (negotiation_reject_body_t *)(s->msg_buf + sizeof(sofh_t) + sizeof(sbe_header_t));
        char reason[49]; memcpy(reason, rej->reason, 48); reason[48] = '\0';
        fprintf(stderr, "[NEGOTIATE] NegotiationReject: error_codes=%u  reason=%.48s\n",
                rej->error_codes, reason);
        return -EACCES;
    } else if (tmpl == TMPL_TERMINATE) {
        terminate_body_t *term =
            (terminate_body_t *)(s->msg_buf + sizeof(sofh_t) + sizeof(sbe_header_t));
        char reason[49]; memcpy(reason, term->reason, 48); reason[48] = '\0';
        fprintf(stderr, "[NEGOTIATE] Server Terminate: error_codes=%u  reason=%.48s\n",
                term->error_codes, reason);
        return -EPROTO;
    } else {
        fprintf(stderr, "[NEGOTIATE] Unexpected templateId=%u\n", tmpl);
        return -EPROTO;
    }
}

/* ── Phase 2: Establish (503) ────────────────────────────────────────────── */

static int do_establish(demo_session_t *s)
{
    uint8_t buf[sizeof(establish_body_t) + sizeof(uint16_t)];
    memset(buf, 0, sizeof(buf));
    establish_body_t *body = (establish_body_t *)buf;

    uint64_t ts = now_ns();
    body->uuid              = s->uuid;
    body->request_timestamp = ts;

    memcpy(body->access_key_id, s->access_key, sizeof(body->access_key_id));
    memcpy(body->session, s->session_id, sizeof(body->session));
    memcpy(body->firm,    s->firm_id,    sizeof(body->firm));
    strncpy(body->trading_system_name,    s->app_name,   sizeof(body->trading_system_name));
    strncpy(body->trading_system_version, s->app_ver,    sizeof(body->trading_system_version));
    strncpy(body->trading_system_vendor,  s->app_vendor, sizeof(body->trading_system_vendor));
    body->next_seq_no         = s->establish_next_seq_no;
    body->keep_alive_interval = KEEPALIVE_MS;

    /* Re-sign with a fresh timestamp for Establish */
    if (sign_establish(s->secret_key_bin, sizeof(s->secret_key_bin),
                       ts, s->uuid,
                       s->session_id, s->firm_id,
                       s->app_name, s->app_ver, s->app_vendor,
                       s->establish_next_seq_no, KEEPALIVE_MS,
                       (uint8_t *)body->hmac_signature) != 0) {
        fprintf(stderr, "[ESTABLISH] HMAC signing failed\n");
        return -EINVAL;
    }

    /* Credentials DATA trailer: zero-length */
    uint16_t *cred_len = (uint16_t *)(buf + sizeof(establish_body_t));
    *cred_len = 0;

    printf("[ESTABLISH] Sending Establish (tmpl=503)\n");

    int rc = send_msg(s, TMPL_ESTABLISH, ESTABLISH_BLOCK_LEN,
                      buf, sizeof(buf));
    if (rc != 0) return rc;

    s->state = S_ESTABLISHING;

    ssize_t n = recv_msg(s);
    if (n < 0) return (int)n;

    uint16_t tmpl = ilink3_parse_template(s->msg_buf, (size_t)n);

    if (tmpl == TMPL_ESTABLISHMENT_ACK) {
        establishment_ack_body_t *ack =
            (establishment_ack_body_t *)(s->msg_buf + sizeof(sofh_t) + sizeof(sbe_header_t));
        s->server_next_seq = ack->next_seq_no;
        printf("[ESTABLISH] EstablishmentAck received\n");
        VLOG("[ESTABLISH] server_next_seq=%u  keep_alive=%u\n",
             s->server_next_seq, ack->keep_alive_interval);
        s->state = S_ACTIVE;
        return 0;
    } else if (tmpl == TMPL_ESTABLISHMENT_REJ) {
        establishment_rej_body_t *rej =
            (establishment_rej_body_t *)(s->msg_buf + sizeof(sofh_t) + sizeof(sbe_header_t));
        char reason[49]; memcpy(reason, rej->reason, 48); reason[48] = '\0';
        fprintf(stderr, "[ESTABLISH] EstablishmentReject: error_codes=%u  reason=%.48s\n",
                rej->error_codes, reason);
        return -EACCES;
    } else if (tmpl == TMPL_TERMINATE) {
        terminate_body_t *term =
            (terminate_body_t *)(s->msg_buf + sizeof(sofh_t) + sizeof(sbe_header_t));
        char reason[49]; memcpy(reason, term->reason, 48); reason[48] = '\0';
        fprintf(stderr, "[ESTABLISH] Server Terminate: error_codes=%u  reason=%.48s\n",
                term->error_codes, reason);
        return -EPROTO;
    } else {
        fprintf(stderr, "[ESTABLISH] Unexpected templateId=%u\n", tmpl);
        return -EPROTO;
    }
}

/* ── Phase 3: Sequence keepalives (506) ──────────────────────────────────── */

static int do_sequence_loop(demo_session_t *s, int rounds)
{
    for (int i = 0; i < rounds; i++) {
        sequence_body_t body;
        body.uuid                       = s->uuid;
        body.next_seq_no                = s->next_seq_no;
        body.fault_tolerance_ind        = ILINK3_FTI_PRIMARY;
        body.keep_alive_interval_lapsed = 0;   /* NotLapsed */

        VLOG("[SEQUENCE] Sending Sequence (tmpl=506)  round=%d  nextSeqNo=%u\n",
             i + 1, s->next_seq_no);

        int rc = send_msg(s, TMPL_SEQUENCE, SEQUENCE_BLOCK_LEN,
                          &body, sizeof(body));
        if (rc != 0) return rc;

        s->next_seq_no++;

        /* Wait for server Sequence keepalive (or any message) */
        ssize_t n = recv_msg(s);
        if (n > 0) {
            uint16_t tmpl = ilink3_parse_template(s->msg_buf, (size_t)n);
            if (tmpl == TMPL_SEQUENCE) {
                sequence_body_t *srv =
                    (sequence_body_t *)(s->msg_buf + sizeof(sofh_t) + sizeof(sbe_header_t));
                s->server_next_seq = srv->next_seq_no;
                VLOG("[SEQUENCE] Server Sequence: server_next_seq=%u\n",
                     s->server_next_seq);
            } else if (tmpl == TMPL_TERMINATE) {
                terminate_body_t *term =
                    (terminate_body_t *)(s->msg_buf + sizeof(sofh_t) + sizeof(sbe_header_t));
                char reason[49]; memcpy(reason, term->reason, 48); reason[48] = '\0';
                fprintf(stderr, "[SEQUENCE] Server Terminate: error_codes=%u  reason=%.48s\n",
                        term->error_codes, reason);
                s->state = S_TERMINATING;
                return 0;
            }
        }

        /* Sleep approximately one heartbeat interval between rounds */
        struct timespec ts = { .tv_sec = HEARTBEAT_SECS, .tv_nsec = 0 };
        nanosleep(&ts, NULL);
    }
    return 0;
}

/* ── Phase 4: Terminate (507) ────────────────────────────────────────────── */

static int do_terminate(demo_session_t *s, const char *reason_str)
{
    terminate_body_t body;
    memset(&body, 0, sizeof(body));

    strncpy(body.reason, reason_str, sizeof(body.reason));
    body.uuid              = s->uuid;
    body.request_timestamp = now_ns();
    body.error_codes       = 0;
    body.split_msg         = U8_NULL;

    VLOG("[TERMINATE] Sending Terminate (tmpl=507)  reason=\"%s\"\n", reason_str);

    int rc = send_msg(s, TMPL_TERMINATE, TERMINATE_BLOCK_LEN,
                      &body, sizeof(body));
    s->state = S_TERMINATING;
    /* No ACK expected – just close TCP */
    return rc;
}

/* ── Resource cleanup ────────────────────────────────────────────────────── */

static void cleanup(demo_session_t *s)
{
    if (s->tcp_sock)   { zft_free(s->tcp_sock);          s->tcp_sock   = NULL; }
    if (s->tcp_handle) { zft_handle_free(s->tcp_handle); s->tcp_handle = NULL; }
    if (s->muxer)      { zf_muxer_free(s->muxer);        s->muxer      = NULL; }
    if (s->stack)      { zf_stack_free(s->stack);         s->stack      = NULL; }
    zf_deinit();
    s->state = S_CLOSED;
    VLOG("[CLEANUP] All TCPDirect resources freed.\n");
}

/* ── Argument parsing ────────────────────────────────────────────────────── */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --host       <ip>   CME MSGW IP address (required)\n"
        "  --port       <port> CME MSGW port       (required)\n"
        "  --interface  <if>   TCPDirect NIC interface (required)\n"
        "  --tcpconnect-only   validate TCP connection only (default: on)\n"
        "  --full-session      run Negotiate/Establish/Sequence/Terminate\n"
        "  --access-key <key>  20-char CME access key ID (required for --full-session)\n"
        "  --secret-key <b64>  43-char Base64URL secret key from CME (required for --full-session)\n"
        "  --session    <str>  CME Session ID, up to 3 chars (required for --full-session)\n"
        "  --firm       <str>  CME Firm ID, up to 5 chars   (required for --full-session)\n"
        "  --app-name   <str>  trading system name    (used by --full-session; default: demo)\n"
        "  --app-ver    <str>  trading system version (used by --full-session; default: 1.0)\n"
        "  --app-vendor <str>  trading system vendor  (used by --full-session; default: demo)\n"
        "  --rounds     <n>    Sequence keepalive rounds for --full-session (default: %d)\n"
        "  --verbose          print detailed protocol/debug logs\n"
        "\n",
        prog, KEEPALIVE_ROUNDS);
}

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    demo_session_t s;
    memset(&s, 0, sizeof(s));

    /* Defaults */
    strncpy(s.app_name,   "demo", sizeof(s.app_name)   - 1);
    strncpy(s.app_ver,    "1.0",  sizeof(s.app_ver)    - 1);
    strncpy(s.app_vendor, "demo", sizeof(s.app_vendor) - 1);
    s.next_seq_no = 1;
    s.server_next_seq = 1;
    s.establish_next_seq_no = 1;
    bool tcpconnect_only = true;
    int  rounds      = KEEPALIVE_ROUNDS;
    char host[64]    = "";
    int  port        = 0;
    char secret_key_str[44] = "";   /* 43 Base64URL chars + NUL */

    static struct option opts[] = {
        {"host",       required_argument, 0, 'h'},
        {"port",       required_argument, 0, 'p'},
        {"interface",  required_argument, 0, 'i'},
        {"tcpconnect-only", no_argument,  0, 't'},
        {"full-session",    no_argument,  0, 'F'},
        {"access-key", required_argument, 0, 'k'},
        {"secret-key", required_argument, 0, 'S'},
        {"session",    required_argument, 0, 's'},
        {"firm",       required_argument, 0, 'f'},
        {"app-name",   required_argument, 0, 'n'},
        {"app-ver",    required_argument, 0, 'v'},
        {"app-vendor", required_argument, 0, 'V'},
        {"rounds",     required_argument, 0, 'r'},
        {"verbose",    no_argument,       0, 'd'},
        {0, 0, 0, 0}
    };

    int c, idx;
    while ((c = getopt_long(argc, argv, "", opts, &idx)) != -1) {
        switch (c) {
        case 'h': strncpy(host,             optarg, sizeof(host)             - 1); break;
        case 'p': port = atoi(optarg);                                              break;
        case 'i': strncpy(s.interface,      optarg, sizeof(s.interface)      - 1); break;
        case 't': tcpconnect_only = true;                                           break;
        case 'F': tcpconnect_only = false;                                          break;
        case 'k': strncpy(s.access_key,     optarg, sizeof(s.access_key)     - 1); break;
        case 'S': strncpy(secret_key_str,   optarg, sizeof(secret_key_str)   - 1); break;
        case 's': strncpy(s.session_id,     optarg, sizeof(s.session_id)     - 1); break;
        case 'f': strncpy(s.firm_id,        optarg, sizeof(s.firm_id)        - 1); break;
        case 'n': strncpy(s.app_name,       optarg, sizeof(s.app_name)       - 1); break;
        case 'v': strncpy(s.app_ver,        optarg, sizeof(s.app_ver)        - 1); break;
        case 'V': strncpy(s.app_vendor,     optarg, sizeof(s.app_vendor)     - 1); break;
        case 'r': rounds = atoi(optarg);                                            break;
        case 'd': g_verbose = true;                                                 break;
        default:  usage(argv[0]); return 1;
        }
    }

    if (host[0] == '\0' || port == 0 || s.interface[0] == '\0') {
        fprintf(stderr, "Error: --host, --port, and --interface are required.\n\n");
        usage(argv[0]); return 1;
    }
    if (! tcpconnect_only) {
        if (secret_key_str[0] == '\0') {
            fprintf(stderr, "Error: --secret-key is required for --full-session.\n\n");
            usage(argv[0]); return 1;
        }

        if (validate_fixed_fields(&s) != 0)
            return 1;

        /* Decode Base64URL secret key → 32 binary bytes */
        size_t secret_len = 0;
        if (base64url_decode(secret_key_str, s.secret_key_bin, &secret_len) != 0
            || secret_len != 32) {
            fprintf(stderr, "Error: --secret-key must be a 43-char Base64URL string "
                            "(decodes to 32 bytes, got %zu)\n", secret_len);
            return 1;
        }
    }

    s.server_addr.sin_family = AF_INET;
    s.server_addr.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &s.server_addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid host address: %s\n", host);
        return 1;
    }

    gen_uuid(&s.uuid);
    printf("[INIT] Session UUID (us-epoch): %llu\n", (unsigned long long)s.uuid);

    /* ── Run the state machine ─────────────────────────────────────────── */

    int rc;

    rc = zf_init_session(&s);
    if (rc != 0) { cleanup(&s); return 1; }

    rc = tcp_connect(&s);
    if (rc != 0) { cleanup(&s); return 1; }

    if (tcpconnect_only) {
        printf("[DONE] TCP connection validated.\n");
        cleanup(&s);
        return 0;
    }

    rc = do_negotiate(&s);
    if (rc != 0) { cleanup(&s); return 1; }

    rc = do_establish(&s);
    if (rc != 0) { cleanup(&s); return 1; }

    if (s.state == S_ACTIVE) {
        rc = do_sequence_loop(&s, rounds);
        if (rc != 0) { cleanup(&s); return 1; }
    }

    if (s.state != S_TERMINATING) {
        do_terminate(&s, "Logout");
    }

    cleanup(&s);
    printf("[DONE] Session closed cleanly.\n");
    return 0;
}
