/*
 * test_base64url.c — unit tests for Base64URL decoding
 */
#include "../framework/test_framework.h"
#include "../../ilink3_auth.h"

/* Known test vector: "Hello" → SGVsbG8 in Base64URL */
TEST(test_decode_hello)
{
    uint8_t out[64];
    size_t len = 0;
    ASSERT_OK(base64url_decode("SGVsbG8", out, &len));
    ASSERT_EQ(len, 5);
    ASSERT_MEM_EQ(out, "Hello", 5);
    return 0;
}

/* Empty input → 0 bytes output */
TEST(test_decode_empty)
{
    uint8_t out[64];
    size_t len = 99;
    ASSERT_OK(base64url_decode("", out, &len));
    ASSERT_EQ(len, 0);
    return 0;
}

/* 32-byte key decode (43 Base64URL chars → 32 bytes) */
TEST(test_decode_32byte_key)
{
    /* 32 bytes of 0xAA → Base64URL: qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqo */
    const char *input = "qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqo";
    uint8_t out[64];
    size_t len = 0;
    ASSERT_OK(base64url_decode(input, out, &len));
    ASSERT_EQ(len, 32);
    return 0;
}

/* Invalid character should fail */
TEST(test_decode_invalid_char)
{
    uint8_t out[64];
    size_t len = 0;
    ASSERT_FAIL(base64url_decode("SGVs!G8", out, &len));
    return 0;
}

/* Base64URL uses - and _ instead of + and / */
TEST(test_decode_url_safe_chars)
{
    /* Standard Base64: +/== → Base64URL: -_ */
    uint8_t out[64];
    size_t len = 0;
    /* "?>" in Base64URL is "Pz4" (P=0x3F, z=0x3E → chars 63,62 → -_) */
    /* Actually test with known: 0xFB,0xFF,0xBE → Base64URL: u_--  */
    ASSERT_OK(base64url_decode("u_--", out, &len));
    ASSERT_EQ(len, 3);
    ASSERT_EQ(out[0], 0xBB);
    ASSERT_EQ(out[1], 0xFF);
    ASSERT_EQ(out[2], 0xBE);
    return 0;
}

/* Padding characters should be handled (stopped at =) */
TEST(test_decode_with_padding)
{
    uint8_t out[64];
    size_t len = 0;
    ASSERT_OK(base64url_decode("SGVsbG8=", out, &len));
    ASSERT_EQ(len, 5);
    ASSERT_MEM_EQ(out, "Hello", 5);
    return 0;
}

/* Single character input */
TEST(test_decode_single_char)
{
    uint8_t out[64];
    size_t len = 0;
    /* 'QQ' decodes to 'A' (0x41) */
    ASSERT_OK(base64url_decode("QQ", out, &len));
    ASSERT_EQ(len, 1);
    ASSERT_EQ(out[0], 'A');
    return 0;
}

int main(int argc, char **argv)
{
    TEST_INIT("test_base64url");

    RUN(test_decode_hello);
    RUN(test_decode_empty);
    RUN(test_decode_32byte_key);
    RUN(test_decode_invalid_char);
    RUN(test_decode_url_safe_chars);
    RUN(test_decode_with_padding);
    RUN(test_decode_single_char);

    return TEST_REPORT();
}
