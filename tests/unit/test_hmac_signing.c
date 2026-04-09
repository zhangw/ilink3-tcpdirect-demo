/*
 * test_hmac_signing.c — unit tests for iLink3 HMAC-SHA256 signing
 */
#include "../framework/test_framework.h"
#include "../../ilink3_auth.h"

/* Fixed 32-byte secret key (all 0x01) for reproducible tests */
static const uint8_t TEST_SECRET[32] = {
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
};

/* sign_negotiate produces 32-byte output */
TEST(test_negotiate_signature_length)
{
    uint8_t sig[SHA256_DIGEST_LENGTH];
    memset(sig, 0, sizeof(sig));
    ASSERT_OK(sign_negotiate(TEST_SECRET, 32,
                             1000000000ULL, 12345ULL,
                             "ABC", "XYZAB", sig));
    /* Verify it's not all zeros (was actually computed) */
    uint8_t zeros[32] = {0};
    ASSERT_TRUE(memcmp(sig, zeros, 32) != 0);
    return 0;
}

/* Same inputs → same signature (deterministic) */
TEST(test_negotiate_deterministic)
{
    uint8_t sig1[SHA256_DIGEST_LENGTH], sig2[SHA256_DIGEST_LENGTH];
    ASSERT_OK(sign_negotiate(TEST_SECRET, 32,
                             999999ULL, 42ULL, "AB", "FIRM1", sig1));
    ASSERT_OK(sign_negotiate(TEST_SECRET, 32,
                             999999ULL, 42ULL, "AB", "FIRM1", sig2));
    ASSERT_MEM_EQ(sig1, sig2, SHA256_DIGEST_LENGTH);
    return 0;
}

/* Different timestamp → different signature */
TEST(test_negotiate_timestamp_sensitivity)
{
    uint8_t sig1[SHA256_DIGEST_LENGTH], sig2[SHA256_DIGEST_LENGTH];
    ASSERT_OK(sign_negotiate(TEST_SECRET, 32,
                             1000ULL, 42ULL, "AB", "FIRM1", sig1));
    ASSERT_OK(sign_negotiate(TEST_SECRET, 32,
                             1001ULL, 42ULL, "AB", "FIRM1", sig2));
    ASSERT_TRUE(memcmp(sig1, sig2, SHA256_DIGEST_LENGTH) != 0);
    return 0;
}

/* Different UUID → different signature */
TEST(test_negotiate_uuid_sensitivity)
{
    uint8_t sig1[SHA256_DIGEST_LENGTH], sig2[SHA256_DIGEST_LENGTH];
    ASSERT_OK(sign_negotiate(TEST_SECRET, 32,
                             1000ULL, 42ULL, "AB", "FIRM1", sig1));
    ASSERT_OK(sign_negotiate(TEST_SECRET, 32,
                             1000ULL, 43ULL, "AB", "FIRM1", sig2));
    ASSERT_TRUE(memcmp(sig1, sig2, SHA256_DIGEST_LENGTH) != 0);
    return 0;
}

/* Different session → different signature */
TEST(test_negotiate_session_sensitivity)
{
    uint8_t sig1[SHA256_DIGEST_LENGTH], sig2[SHA256_DIGEST_LENGTH];
    ASSERT_OK(sign_negotiate(TEST_SECRET, 32,
                             1000ULL, 42ULL, "AB", "FIRM1", sig1));
    ASSERT_OK(sign_negotiate(TEST_SECRET, 32,
                             1000ULL, 42ULL, "CD", "FIRM1", sig2));
    ASSERT_TRUE(memcmp(sig1, sig2, SHA256_DIGEST_LENGTH) != 0);
    return 0;
}

/* sign_establish produces valid output */
TEST(test_establish_signature_length)
{
    uint8_t sig[SHA256_DIGEST_LENGTH];
    ASSERT_OK(sign_establish(TEST_SECRET, 32,
                             1000000000ULL, 12345ULL,
                             "ABC", "XYZAB",
                             "demo", "1.0", "demo",
                             1, 10000, sig));
    uint8_t zeros[32] = {0};
    ASSERT_TRUE(memcmp(sig, zeros, 32) != 0);
    return 0;
}

/* Establish: different next_seq_no → different signature */
TEST(test_establish_seqno_sensitivity)
{
    uint8_t sig1[SHA256_DIGEST_LENGTH], sig2[SHA256_DIGEST_LENGTH];
    ASSERT_OK(sign_establish(TEST_SECRET, 32,
                             1000ULL, 42ULL, "AB", "FIRM1",
                             "app", "1.0", "vendor", 1, 10000, sig1));
    ASSERT_OK(sign_establish(TEST_SECRET, 32,
                             1000ULL, 42ULL, "AB", "FIRM1",
                             "app", "1.0", "vendor", 2, 10000, sig2));
    ASSERT_TRUE(memcmp(sig1, sig2, SHA256_DIGEST_LENGTH) != 0);
    return 0;
}

/* Establish: different keepalive → different signature */
TEST(test_establish_keepalive_sensitivity)
{
    uint8_t sig1[SHA256_DIGEST_LENGTH], sig2[SHA256_DIGEST_LENGTH];
    ASSERT_OK(sign_establish(TEST_SECRET, 32,
                             1000ULL, 42ULL, "AB", "FIRM1",
                             "app", "1.0", "vendor", 1, 10000, sig1));
    ASSERT_OK(sign_establish(TEST_SECRET, 32,
                             1000ULL, 42ULL, "AB", "FIRM1",
                             "app", "1.0", "vendor", 1, 20000, sig2));
    ASSERT_TRUE(memcmp(sig1, sig2, SHA256_DIGEST_LENGTH) != 0);
    return 0;
}

/* Different secret key → different signature */
TEST(test_negotiate_key_sensitivity)
{
    uint8_t key2[32];
    memset(key2, 0x02, 32);
    uint8_t sig1[SHA256_DIGEST_LENGTH], sig2[SHA256_DIGEST_LENGTH];
    ASSERT_OK(sign_negotiate(TEST_SECRET, 32,
                             1000ULL, 42ULL, "AB", "FIRM1", sig1));
    ASSERT_OK(sign_negotiate(key2, 32,
                             1000ULL, 42ULL, "AB", "FIRM1", sig2));
    ASSERT_TRUE(memcmp(sig1, sig2, SHA256_DIGEST_LENGTH) != 0);
    return 0;
}

int main(int argc, char **argv)
{
    TEST_INIT("test_hmac_signing");

    RUN(test_negotiate_signature_length);
    RUN(test_negotiate_deterministic);
    RUN(test_negotiate_timestamp_sensitivity);
    RUN(test_negotiate_uuid_sensitivity);
    RUN(test_negotiate_session_sensitivity);
    RUN(test_establish_signature_length);
    RUN(test_establish_seqno_sensitivity);
    RUN(test_establish_keepalive_sensitivity);
    RUN(test_negotiate_key_sensitivity);

    return TEST_REPORT();
}
