CC     = gcc
CFLAGS = -O2 -Wall -Wextra -std=gnu11

ZF_INCLUDE ?= /usr/include
ZF_LIB     ?= /usr/lib

CLIENT_DYN    = ilink3_client
CLIENT_STATIC = ilink3_client_static
SERVER_TARGET = ilink3_server

CFLAGS  += -I$(ZF_INCLUDE)
LDFLAGS_DYN    = -L$(ZF_LIB) -Wl,-rpath,$(ZF_LIB) -lonload_zf -lcrypto
LDFLAGS_STATIC = $(ZF_LIB)/libonload_zf_static.a \
                 $(shell pkg-config --variable=libdir libcrypto 2>/dev/null || echo /usr/lib/x86_64-linux-gnu)/libcrypto.a \
                 -lpthread -ldl -lm

HEADERS = ilink3_proto.h ilink3_sbe.h ilink3_auth.h

.PHONY: all server test clean

all: $(CLIENT_DYN) $(CLIENT_STATIC) $(SERVER_TARGET)

server: $(SERVER_TARGET)

$(CLIENT_DYN): ilink3_client.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ ilink3_client.c $(LDFLAGS_DYN)

$(CLIENT_STATIC): ilink3_client.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ ilink3_client.c $(LDFLAGS_STATIC)

$(SERVER_TARGET): ilink3_server.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ ilink3_server.c $(LDFLAGS_DYN)

# ── Test targets ─────────────────────────────────────────────────────────
TEST_CFLAGS = -O0 -Wall -Wextra -std=gnu11 -g
TEST_LDFLAGS_CRYPTO = -lcrypto
TEST_LDFLAGS_ZF = -L$(ZF_LIB) -Wl,-rpath,$(ZF_LIB) -lonload_zf -lcrypto -lm

test: test_base64url test_hmac_signing test_sbe_framing test_validation \
      test_frame_extract perf_session

test_base64url: tests/unit/test_base64url.c ilink3_auth.h ilink3_sbe.h
	$(CC) $(TEST_CFLAGS) -o $@ $< $(TEST_LDFLAGS_CRYPTO)

test_hmac_signing: tests/unit/test_hmac_signing.c ilink3_auth.h ilink3_sbe.h
	$(CC) $(TEST_CFLAGS) -o $@ $< $(TEST_LDFLAGS_CRYPTO)

test_sbe_framing: tests/unit/test_sbe_framing.c ilink3_sbe.h
	$(CC) $(TEST_CFLAGS) -o $@ $<

test_validation: tests/unit/test_validation.c ilink3_auth.h ilink3_sbe.h
	$(CC) $(TEST_CFLAGS) -o $@ $< $(TEST_LDFLAGS_CRYPTO)

test_frame_extract: tests/unit/test_frame_extract.c ilink3_sbe.h
	$(CC) $(TEST_CFLAGS) -o $@ $<

perf_session: tests/perf/perf_session.c $(HEADERS) ilink3_auth.h
	$(CC) $(TEST_CFLAGS) -I$(ZF_INCLUDE) -o $@ $< $(TEST_LDFLAGS_ZF)

clean:
	rm -f $(CLIENT_DYN) $(CLIENT_STATIC) $(SERVER_TARGET)
	rm -f test_base64url test_hmac_signing test_sbe_framing test_validation
	rm -f test_frame_extract perf_session
	rm -f tests/results/*.json
