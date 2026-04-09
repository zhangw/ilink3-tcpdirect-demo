/*
 * test_sbe_framing.c — unit tests for SBE wire format, struct packing, framing helpers
 */
#include "../framework/test_framework.h"
#include "../../ilink3_sbe.h"

/* ── Struct size tests (must match CME schema block lengths) ─────────────── */

TEST(test_sofh_size)
{
    ASSERT_EQ(sizeof(sofh_t), 4);
    return 0;
}

TEST(test_sbe_header_size)
{
    ASSERT_EQ(sizeof(sbe_header_t), 8);
    return 0;
}

TEST(test_negotiate_body_size)
{
    ASSERT_EQ(sizeof(negotiate_body_t), NEGOTIATE_BLOCK_LEN);
    return 0;
}

TEST(test_negotiation_resp_body_size)
{
    ASSERT_EQ(sizeof(negotiation_resp_body_t), NEGOTIATION_RESP_BLOCK_LEN);
    return 0;
}

TEST(test_establish_body_size)
{
    ASSERT_EQ(sizeof(establish_body_t), ESTABLISH_BLOCK_LEN);
    return 0;
}

TEST(test_establishment_ack_body_size)
{
    ASSERT_EQ(sizeof(establishment_ack_body_t), ESTABLISHMENT_ACK_BLOCK_LEN);
    return 0;
}

TEST(test_sequence_body_size)
{
    ASSERT_EQ(sizeof(sequence_body_t), SEQUENCE_BLOCK_LEN);
    return 0;
}

TEST(test_terminate_body_size)
{
    ASSERT_EQ(sizeof(terminate_body_t), TERMINATE_BLOCK_LEN);
    return 0;
}

/* ── ilink3_frame_size ───────────────────────────────────────────────────── */

TEST(test_frame_size_negotiate)
{
    size_t expected = sizeof(sofh_t) + sizeof(sbe_header_t) +
                      sizeof(negotiate_body_t) + sizeof(uint16_t);
    ASSERT_EQ(ilink3_frame_size(sizeof(negotiate_body_t) + sizeof(uint16_t)),
              expected);
    return 0;
}

TEST(test_frame_size_sequence)
{
    size_t expected = sizeof(sofh_t) + sizeof(sbe_header_t) + sizeof(sequence_body_t);
    ASSERT_EQ(ilink3_frame_size(sizeof(sequence_body_t)), expected);
    return 0;
}

TEST(test_frame_size_zero_body)
{
    ASSERT_EQ(ilink3_frame_size(0), sizeof(sofh_t) + sizeof(sbe_header_t));
    return 0;
}

/* ── ilink3_write_headers ────────────────────────────────────────────────── */

TEST(test_write_headers_sofh)
{
    uint8_t buf[128];
    memset(buf, 0, sizeof(buf));
    size_t frame_len = ilink3_frame_size(sizeof(sequence_body_t));
    ilink3_write_headers(buf, TMPL_SEQUENCE, SEQUENCE_BLOCK_LEN, frame_len);

    sofh_t *sofh = (sofh_t *)buf;
    ASSERT_EQ(sofh->message_size, frame_len);
    ASSERT_EQ(sofh->encoding_type, ILINK3_ENCODING_TYPE);
    return 0;
}

TEST(test_write_headers_sbe)
{
    uint8_t buf[128];
    memset(buf, 0, sizeof(buf));
    size_t frame_len = ilink3_frame_size(sizeof(negotiate_body_t) + 2);
    ilink3_write_headers(buf, TMPL_NEGOTIATE, NEGOTIATE_BLOCK_LEN, frame_len);

    sbe_header_t *hdr = (sbe_header_t *)(buf + sizeof(sofh_t));
    ASSERT_EQ(hdr->block_length, NEGOTIATE_BLOCK_LEN);
    ASSERT_EQ(hdr->template_id, TMPL_NEGOTIATE);
    ASSERT_EQ(hdr->schema_id, ILINK3_SCHEMA_ID);
    ASSERT_EQ(hdr->version, ILINK3_SCHEMA_VER);
    return 0;
}

TEST(test_write_headers_returns_body_ptr)
{
    uint8_t buf[128];
    uint8_t *body = ilink3_write_headers(buf, TMPL_SEQUENCE, SEQUENCE_BLOCK_LEN,
                                         ilink3_frame_size(sizeof(sequence_body_t)));
    ASSERT_EQ((size_t)(body - buf), sizeof(sofh_t) + sizeof(sbe_header_t));
    return 0;
}

/* ── ilink3_parse_template ───────────────────────────────────────────────── */

TEST(test_parse_template_valid)
{
    uint8_t buf[128];
    size_t frame_len = ilink3_frame_size(sizeof(sequence_body_t));
    ilink3_write_headers(buf, TMPL_SEQUENCE, SEQUENCE_BLOCK_LEN, frame_len);
    ASSERT_EQ(ilink3_parse_template(buf, frame_len), TMPL_SEQUENCE);
    return 0;
}

TEST(test_parse_template_negotiate)
{
    uint8_t buf[128];
    size_t frame_len = ilink3_frame_size(sizeof(negotiate_body_t) + 2);
    ilink3_write_headers(buf, TMPL_NEGOTIATE, NEGOTIATE_BLOCK_LEN, frame_len);
    ASSERT_EQ(ilink3_parse_template(buf, frame_len), TMPL_NEGOTIATE);
    return 0;
}

TEST(test_parse_template_truncated)
{
    uint8_t buf[4] = {0};
    ASSERT_EQ(ilink3_parse_template(buf, 4), 0);
    return 0;
}

TEST(test_parse_template_wrong_encoding)
{
    uint8_t buf[128];
    size_t frame_len = ilink3_frame_size(sizeof(sequence_body_t));
    ilink3_write_headers(buf, TMPL_SEQUENCE, SEQUENCE_BLOCK_LEN, frame_len);
    /* Corrupt encoding type */
    sofh_t *sofh = (sofh_t *)buf;
    sofh->encoding_type = 0xDEAD;
    ASSERT_EQ(ilink3_parse_template(buf, frame_len), 0);
    return 0;
}

TEST(test_parse_template_wrong_schema)
{
    uint8_t buf[128];
    size_t frame_len = ilink3_frame_size(sizeof(sequence_body_t));
    ilink3_write_headers(buf, TMPL_SEQUENCE, SEQUENCE_BLOCK_LEN, frame_len);
    /* Corrupt schema ID */
    sbe_header_t *hdr = (sbe_header_t *)(buf + sizeof(sofh_t));
    hdr->schema_id = 99;
    ASSERT_EQ(ilink3_parse_template(buf, frame_len), 0);
    return 0;
}

TEST(test_parse_template_size_exceeds_buffer)
{
    uint8_t buf[128];
    size_t frame_len = ilink3_frame_size(sizeof(sequence_body_t));
    ilink3_write_headers(buf, TMPL_SEQUENCE, SEQUENCE_BLOCK_LEN, frame_len);
    /* Pass smaller buffer length than SOFH claims */
    ASSERT_EQ(ilink3_parse_template(buf, frame_len - 1), 0);
    return 0;
}

/* ── Template ID constants ───────────────────────────────────────────────── */

TEST(test_template_id_values)
{
    ASSERT_EQ(TMPL_NEGOTIATE, 500);
    ASSERT_EQ(TMPL_NEGOTIATION_RESP, 501);
    ASSERT_EQ(TMPL_NEGOTIATION_REJECT, 502);
    ASSERT_EQ(TMPL_ESTABLISH, 503);
    ASSERT_EQ(TMPL_ESTABLISHMENT_ACK, 504);
    ASSERT_EQ(TMPL_ESTABLISHMENT_REJ, 505);
    ASSERT_EQ(TMPL_SEQUENCE, 506);
    ASSERT_EQ(TMPL_TERMINATE, 507);
    return 0;
}

/* ── Encoding constants ──────────────────────────────────────────────────── */

TEST(test_schema_constants)
{
    ASSERT_EQ(ILINK3_SCHEMA_ID, 8);
    ASSERT_EQ(ILINK3_SCHEMA_VER, 9);
    ASSERT_EQ(ILINK3_ENCODING_TYPE, 0xCAFE);
    return 0;
}

int main(int argc, char **argv)
{
    TEST_INIT("test_sbe_framing");

    RUN(test_sofh_size);
    RUN(test_sbe_header_size);
    RUN(test_negotiate_body_size);
    RUN(test_negotiation_resp_body_size);
    RUN(test_establish_body_size);
    RUN(test_establishment_ack_body_size);
    RUN(test_sequence_body_size);
    RUN(test_terminate_body_size);
    RUN(test_frame_size_negotiate);
    RUN(test_frame_size_sequence);
    RUN(test_frame_size_zero_body);
    RUN(test_write_headers_sofh);
    RUN(test_write_headers_sbe);
    RUN(test_write_headers_returns_body_ptr);
    RUN(test_parse_template_valid);
    RUN(test_parse_template_negotiate);
    RUN(test_parse_template_truncated);
    RUN(test_parse_template_wrong_encoding);
    RUN(test_parse_template_wrong_schema);
    RUN(test_parse_template_size_exceeds_buffer);
    RUN(test_template_id_values);
    RUN(test_schema_constants);

    return TEST_REPORT();
}
