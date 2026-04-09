/*
 * test_frame_extract.c — unit tests for extract_frame (from ilink3_proto.h)
 *
 * extract_frame operates purely on the rx_buf/msg_buf in ilink3_transport_t,
 * no ZF calls needed. We can test it by populating the buffer directly.
 */

#include <stdbool.h>

/* Provide the g_verbose that ilink3_proto.h expects */
static bool g_verbose = false;

/* Stub out ZF headers — extract_frame doesn't call any ZF functions */
#define _ZF_ZF_H
#define _ZF_ZF_TCP_H
#include <sys/epoll.h>
#include <sys/uio.h>
struct zf_stack;
struct zf_muxer_set;
struct zft;
struct zftl;
struct zf_attr;

#include "../framework/test_framework.h"
#include "../../ilink3_sbe.h"

/* Minimal subset of ilink3_proto.h that we need — just the transport struct
 * and extract_frame. We can't include the full header because it pulls in
 * zf/zf.h. So we replicate the pieces. */

#define RX_BUF_LEN  4096u
#define TX_BUF_LEN  4096u
#define VLOG(...) do { if (g_verbose) fprintf(stderr, __VA_ARGS__); } while(0)

typedef struct {
    struct zf_stack     *stack;
    struct zf_muxer_set *muxer;
    struct zft          *tcp_sock;
    uint8_t  rx_buf[RX_BUF_LEN];
    size_t   rx_buf_len;
    uint8_t  msg_buf[RX_BUF_LEN];
    uint8_t  tx_buf[TX_BUF_LEN];
} ilink3_transport_t;

/* Copy extract_frame from ilink3_proto.h (it's static inline) */
static inline ssize_t extract_frame(ilink3_transport_t *t)
{
    if (t->rx_buf_len < sizeof(sofh_t) + sizeof(sbe_header_t))
        return 0;

    const sofh_t *sofh = (const sofh_t *)t->rx_buf;
    if (sofh->encoding_type != ILINK3_ENCODING_TYPE) {
        fprintf(stderr, "[RX] Invalid SOFH encoding type: 0x%04x\n",
                sofh->encoding_type);
        return -71; /* EPROTO */
    }
    if (sofh->message_size < sizeof(sofh_t) + sizeof(sbe_header_t)) {
        fprintf(stderr, "[RX] Invalid SOFH message size: %u\n", sofh->message_size);
        return -71;
    }
    if (sofh->message_size > RX_BUF_LEN) {
        fprintf(stderr, "[RX] Frame too large for buffer: %u\n", sofh->message_size);
        return -90; /* EMSGSIZE */
    }
    if (t->rx_buf_len < sofh->message_size)
        return 0;

    uint16_t tmpl = ilink3_parse_template(t->rx_buf, t->rx_buf_len);
    if (tmpl == 0) {
        fprintf(stderr, "[RX] Invalid SBE frame len=%zu\n", t->rx_buf_len);
        return -71;
    }

    size_t frame_len = sofh->message_size;
    memcpy(t->msg_buf, t->rx_buf, frame_len);
    t->rx_buf_len -= frame_len;
    if (t->rx_buf_len > 0)
        memmove(t->rx_buf, t->rx_buf + frame_len, t->rx_buf_len);

    VLOG("[RX] templateId=%u  len=%zu\n", tmpl, frame_len);
    return (ssize_t)frame_len;
}

/* Helper: write a valid frame into a buffer */
static size_t write_test_frame(uint8_t *buf, uint16_t tmpl, uint16_t block_len,
                               size_t body_len)
{
    size_t frame_len = sizeof(sofh_t) + sizeof(sbe_header_t) + body_len;
    ilink3_write_headers(buf, tmpl, block_len, frame_len);
    memset(buf + sizeof(sofh_t) + sizeof(sbe_header_t), 0xAA, body_len);
    return frame_len;
}

/* ── Tests ───────────────────────────────────────────────────────────────── */

TEST(test_extract_complete_frame)
{
    ilink3_transport_t t;
    memset(&t, 0, sizeof(t));
    t.rx_buf_len = write_test_frame(t.rx_buf, TMPL_SEQUENCE,
                                     SEQUENCE_BLOCK_LEN, sizeof(sequence_body_t));
    ssize_t n = extract_frame(&t);
    ASSERT_TRUE(n > 0);
    ASSERT_EQ(t.rx_buf_len, 0);  /* consumed */
    /* Verify msg_buf has the frame */
    uint16_t tmpl = ilink3_parse_template(t.msg_buf, (size_t)n);
    ASSERT_EQ(tmpl, TMPL_SEQUENCE);
    return 0;
}

TEST(test_extract_partial_frame_returns_zero)
{
    ilink3_transport_t t;
    memset(&t, 0, sizeof(t));
    size_t full = write_test_frame(t.rx_buf, TMPL_SEQUENCE,
                                    SEQUENCE_BLOCK_LEN, sizeof(sequence_body_t));
    t.rx_buf_len = full - 1;  /* one byte short */
    ssize_t n = extract_frame(&t);
    ASSERT_EQ(n, 0);
    ASSERT_EQ(t.rx_buf_len, full - 1);  /* unchanged */
    return 0;
}

TEST(test_extract_too_small_buffer)
{
    ilink3_transport_t t;
    memset(&t, 0, sizeof(t));
    t.rx_buf_len = 5;  /* less than SOFH + SBE header */
    ssize_t n = extract_frame(&t);
    ASSERT_EQ(n, 0);
    return 0;
}

TEST(test_extract_wrong_encoding_type)
{
    ilink3_transport_t t;
    memset(&t, 0, sizeof(t));
    t.rx_buf_len = write_test_frame(t.rx_buf, TMPL_SEQUENCE,
                                     SEQUENCE_BLOCK_LEN, sizeof(sequence_body_t));
    /* Corrupt encoding type */
    sofh_t *sofh = (sofh_t *)t.rx_buf;
    sofh->encoding_type = 0xBEEF;
    ssize_t n = extract_frame(&t);
    ASSERT_TRUE(n < 0);  /* error */
    return 0;
}

TEST(test_extract_two_frames_in_buffer)
{
    ilink3_transport_t t;
    memset(&t, 0, sizeof(t));

    /* Write two frames back-to-back */
    size_t f1 = write_test_frame(t.rx_buf, TMPL_SEQUENCE,
                                  SEQUENCE_BLOCK_LEN, sizeof(sequence_body_t));
    size_t f2 = write_test_frame(t.rx_buf + f1, TMPL_TERMINATE,
                                  TERMINATE_BLOCK_LEN, sizeof(terminate_body_t));
    t.rx_buf_len = f1 + f2;

    /* Extract first */
    ssize_t n1 = extract_frame(&t);
    ASSERT_TRUE(n1 > 0);
    ASSERT_EQ(ilink3_parse_template(t.msg_buf, (size_t)n1), TMPL_SEQUENCE);
    ASSERT_EQ(t.rx_buf_len, f2);

    /* Extract second */
    ssize_t n2 = extract_frame(&t);
    ASSERT_TRUE(n2 > 0);
    ASSERT_EQ(ilink3_parse_template(t.msg_buf, (size_t)n2), TMPL_TERMINATE);
    ASSERT_EQ(t.rx_buf_len, 0);

    return 0;
}

TEST(test_extract_negotiate_frame)
{
    ilink3_transport_t t;
    memset(&t, 0, sizeof(t));
    t.rx_buf_len = write_test_frame(t.rx_buf, TMPL_NEGOTIATE,
                                     NEGOTIATE_BLOCK_LEN,
                                     sizeof(negotiate_body_t) + sizeof(uint16_t));
    ssize_t n = extract_frame(&t);
    ASSERT_TRUE(n > 0);
    ASSERT_EQ(ilink3_parse_template(t.msg_buf, (size_t)n), TMPL_NEGOTIATE);
    return 0;
}

int main(int argc, char **argv)
{
    TEST_INIT("test_frame_extract");

    RUN(test_extract_complete_frame);
    RUN(test_extract_partial_frame_returns_zero);
    RUN(test_extract_too_small_buffer);
    RUN(test_extract_wrong_encoding_type);
    RUN(test_extract_two_frames_in_buffer);
    RUN(test_extract_negotiate_frame);

    return TEST_REPORT();
}
