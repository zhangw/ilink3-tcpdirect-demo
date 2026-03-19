/*
 * ilink3_server.c
 *
 * Minimal CME iLink3 v9 loopback server using TCPDirect (libonload_zf.so).
 * Accepts one client connection, handles the full session lifecycle, and exits.
 * HMAC signatures are NOT validated — for loopback/demo use only.
 *
 * Protocol flow handled:
 *   TCP accept → Negotiate(500) → NegotiationResponse(501)
 *              → Establish(503) → EstablishmentAck(504)
 *              → Sequence(506) ↔ Sequence(506)  (N rounds)
 *              → Terminate(507) → TCP close
 *
 * Build:
 *   make server
 *
 * Run:
 *   ./ilink3_server --interface <iface> --port <port> [--verbose]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <getopt.h>

static bool g_verbose = false;

#include "ilink3_proto.h"

/* ── Server session state ────────────────────────────────────────────────── */

typedef struct {
    ilink3_transport_t  t;             /* embedded transport                 */
    struct zftl        *listener;
    session_state_t     state;
    ilink3_uuid_t       server_uuid;
    SeqNum              server_next_seq_no;
    uint16_t            port;
    char                interface[32];
    char                ip[32];        /* bind address (ZF requires specific IP, not INADDR_ANY) */
} server_session_t;

/* ── Resource cleanup ────────────────────────────────────────────────────── */

static void cleanup(server_session_t *s)
{
    if (s->t.tcp_sock) { zft_free(s->t.tcp_sock);      s->t.tcp_sock = NULL; }
    if (s->listener)   { zftl_free(s->listener);        s->listener   = NULL; }
    if (s->t.muxer)    { zf_muxer_free(s->t.muxer);    s->t.muxer    = NULL; }
    if (s->t.stack)    { zf_stack_free(s->t.stack);     s->t.stack    = NULL; }
    zf_deinit();
    s->state = S_CLOSED;
    VLOG("[CLEANUP] All TCPDirect resources freed.\n");
}

/* ── Message handlers ────────────────────────────────────────────────────── */

static int handle_negotiate(server_session_t *s)
{
    negotiate_body_t *neg =
        (negotiate_body_t *)(s->t.msg_buf + sizeof(sofh_t) + sizeof(sbe_header_t));

    /* Build NegotiationResponse (501) with Credentials DATA trailer */
    uint8_t resp_buf[sizeof(negotiation_resp_body_t) + sizeof(uint16_t)];
    memset(resp_buf, 0, sizeof(resp_buf));
    negotiation_resp_body_t *resp = (negotiation_resp_body_t *)resp_buf;

    resp->uuid                     = neg->uuid;
    resp->request_timestamp        = neg->request_timestamp;
    resp->secret_key_secure_id_exp = U16_NULL;
    resp->fault_tolerance_ind      = ILINK3_FTI_PRIMARY;
    resp->split_msg                = U8_NULL;
    resp->prev_seq_no              = 0;
    resp->prev_uuid                = 0;
    resp->environment_indicator    = U8_NULL;
    /* Credentials DATA trailer uint16_t(0) already zeroed by memset */

    printf("[SERVER] Sending NegotiationResponse (tmpl=501)\n");
    int rc = send_msg(&s->t, TMPL_NEGOTIATION_RESP, NEGOTIATION_RESP_BLOCK_LEN, resp_buf, sizeof(resp_buf));
    if (rc != 0) return rc;

    s->state = S_NEGOTIATED;
    return 0;
}

static int handle_establish(server_session_t *s)
{
    establish_body_t *est =
        (establish_body_t *)(s->t.msg_buf + sizeof(sofh_t) + sizeof(sbe_header_t));

    establishment_ack_body_t ack;
    memset(&ack, 0, sizeof(ack));

    ack.uuid                     = est->uuid;
    ack.request_timestamp        = est->request_timestamp;
    ack.next_seq_no              = 1;
    ack.prev_seq_no              = 0;
    ack.prev_uuid                = 0;
    ack.keep_alive_interval      = est->keep_alive_interval;
    ack.secret_key_secure_id_exp = U16_NULL;
    ack.fault_tolerance_ind      = ILINK3_FTI_PRIMARY;
    ack.split_msg                = U8_NULL;
    ack.environment_indicator    = U8_NULL;

    printf("[SERVER] Sending EstablishmentAck (tmpl=504)\n");
    int rc = send_msg(&s->t, TMPL_ESTABLISHMENT_ACK, ESTABLISHMENT_ACK_BLOCK_LEN, &ack, sizeof(ack));
    if (rc != 0) return rc;

    s->state = S_ACTIVE;
    return 0;
}

static int handle_sequence(server_session_t *s)
{
    sequence_body_t seq;
    memset(&seq, 0, sizeof(seq));

    seq.uuid                       = s->server_uuid;
    seq.next_seq_no                = s->server_next_seq_no++;
    seq.fault_tolerance_ind        = ILINK3_FTI_PRIMARY;
    seq.keep_alive_interval_lapsed = 0;

    VLOG("[SERVER] Sending Sequence (tmpl=506)  nextSeqNo=%u\n", seq.next_seq_no);
    return send_msg(&s->t, TMPL_SEQUENCE, SEQUENCE_BLOCK_LEN, &seq, sizeof(seq));
}

/* ── Server event loop ───────────────────────────────────────────────────── */

static int server_run(server_session_t *s)
{
    struct epoll_event ev;

    /* Accept loop */
    printf("[SERVER] Waiting for connection on port %u...\n", s->port);
    while (s->state == S_LISTENING) {
        zf_reactor_perform(s->t.stack);
        zf_muxer_wait(s->t.muxer, &ev, 1, POLL_TIMEOUT_NS);

        int rc = zftl_accept(s->listener, &s->t.tcp_sock);
        if (rc == 0) {
            s->state = S_TCP_ACCEPTED;
            printf("[SERVER] Connection accepted\n");
            break;
        }
        if (rc != -EAGAIN) {
            fprintf(stderr, "[SERVER] zftl_accept failed: %d\n", rc);
            return rc;
        }
    }

    /* Message loop */
    while (s->state != S_CLOSED) {
        ssize_t n = recv_msg(&s->t);
        if (n < 0) {
            if (n == -EPIPE) break;   /* client closed cleanly */
            fprintf(stderr, "[SERVER] recv_msg error: %zd\n", n);
            return (int)n;
        }

        uint16_t tmpl = ilink3_parse_template(s->t.msg_buf, (size_t)n);
        VLOG("[SERVER] Received templateId=%u\n", tmpl);

        int rc = 0;
        switch (tmpl) {
        case TMPL_NEGOTIATE:
            s->state = S_NEGOTIATING;
            rc = handle_negotiate(s);
            break;
        case TMPL_ESTABLISH:
            s->state = S_ESTABLISHING;
            rc = handle_establish(s);
            break;
        case TMPL_SEQUENCE:
            rc = handle_sequence(s);
            break;
        case TMPL_TERMINATE:
            printf("[SERVER] Terminate received, closing\n");
            s->state = S_CLOSED;
            break;
        default:
            fprintf(stderr, "[SERVER] Unexpected templateId=%u\n", tmpl);
            break;
        }
        if (rc != 0) return rc;
    }

    return 0;
}

/* ── Argument parsing / main ─────────────────────────────────────────────── */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s --interface <iface> --ip <addr> --port <port> [--verbose]\n",
        prog);
}

int main(int argc, char *argv[])
{
    server_session_t s;
    memset(&s, 0, sizeof(s));
    s.server_next_seq_no = 1;

    static struct option opts[] = {
        {"interface", required_argument, 0, 'i'},
        {"ip",        required_argument, 0, 'a'},
        {"port",      required_argument, 0, 'p'},
        {"verbose",   no_argument,       0, 'd'},
        {0, 0, 0, 0}
    };

    int c, idx;
    while ((c = getopt_long(argc, argv, "", opts, &idx)) != -1) {
        switch (c) {
        case 'i': strncpy(s.interface, optarg, sizeof(s.interface) - 1); break;
        case 'a': strncpy(s.ip,        optarg, sizeof(s.ip)        - 1); break;
        case 'p': s.port = (uint16_t)atoi(optarg);                       break;
        case 'd': g_verbose = true;                                       break;
        default:  usage(argv[0]); return 1;
        }
    }

    if (s.interface[0] == '\0' || s.ip[0] == '\0' || s.port == 0) {
        fprintf(stderr, "Error: --interface, --ip, and --port are required.\n\n");
        usage(argv[0]); return 1;
    }

    /* Init ZF stack */
    int rc = zf_stack_init(&s.t, s.interface);
    if (rc != 0) return 1;

    gen_uuid(&s.server_uuid);

    /* Create TCP listener */
    struct zf_attr *attr = NULL;
    zf_attr_alloc(&attr);
    zf_attr_set_str(attr, "interface", s.interface);

    struct sockaddr_in laddr;
    memset(&laddr, 0, sizeof(laddr));
    laddr.sin_family      = AF_INET;
    laddr.sin_addr.s_addr = inet_addr(s.ip);
    laddr.sin_port        = htons(s.port);

    rc = zftl_listen(s.t.stack, (struct sockaddr *)&laddr, sizeof(laddr),
                     attr, &s.listener);
    zf_attr_free(attr);
    if (rc != 0) {
        fprintf(stderr, "zftl_alloc() failed: %d\n", rc);
        cleanup(&s);
        return 1;
    }

    s.state = S_LISTENING;

    rc = server_run(&s);
    cleanup(&s);
    if (rc == 0) printf("[SERVER] Done.\n");
    return rc ? 1 : 0;
}
