#pragma once
/*
 * ilink3_auth.h — extracted pure functions for testability
 *
 * Contains: base64url_decode, HMAC signing, field validation.
 * Included by ilink3_client.c and unit test binaries.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <openssl/hmac.h>
#include <openssl/sha.h>

#include "ilink3_sbe.h"

/* ── Base64URL decode (RFC 4648 §5) ──────────────────────────────────────── */

static inline int base64url_decode(const char *in, uint8_t *out, size_t *out_len)
{
    static const int8_t T[128] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,63,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
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

/* ── HMAC signing ────────────────────────────────────────────────────────── */

static inline int sign_negotiate(const uint8_t *secret, size_t secret_len,
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
    unsigned int md_len = SHA256_DIGEST_LENGTH;
    return HMAC(EVP_sha256(), secret, (int)secret_len,
                (const unsigned char *)canonical, (size_t)n,
                out, &md_len) ? 0 : -1;
}

static inline int sign_establish(const uint8_t *secret, size_t secret_len,
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
    unsigned int md_len = SHA256_DIGEST_LENGTH;
    return HMAC(EVP_sha256(), secret, (int)secret_len,
                (const unsigned char *)canonical, (size_t)n,
                out, &md_len) ? 0 : -1;
}

/* ── Field validation ────────────────────────────────────────────────────── */

typedef struct {
    char access_key[21];
    char session_id[4];
    char firm_id[6];
    char interface[32];
    char app_name[21];
    char app_ver[11];
    char app_vendor[11];
} ilink3_fields_t;

static inline int validate_fixed_fields(const ilink3_fields_t *f)
{
    size_t access_key_len = strlen(f->access_key);
    size_t session_len    = strlen(f->session_id);
    size_t firm_len       = strlen(f->firm_id);
    size_t interface_len  = strlen(f->interface);
    size_t app_name_len   = strlen(f->app_name);
    size_t app_ver_len    = strlen(f->app_ver);
    size_t app_vendor_len = strlen(f->app_vendor);

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
    if (interface_len == 0 || interface_len >= sizeof(f->interface)) {
        fprintf(stderr, "Error: --interface must be 1-%zu chars\n",
                sizeof(f->interface) - 1);
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
