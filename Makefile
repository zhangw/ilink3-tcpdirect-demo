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

.PHONY: all server clean

all: $(CLIENT_DYN) $(CLIENT_STATIC) $(SERVER_TARGET)

server: $(SERVER_TARGET)

$(CLIENT_DYN): ilink3_client.c ilink3_proto.h ilink3_sbe.h
	$(CC) $(CFLAGS) -o $@ ilink3_client.c $(LDFLAGS_DYN)

$(CLIENT_STATIC): ilink3_client.c ilink3_proto.h ilink3_sbe.h
	$(CC) $(CFLAGS) -o $@ ilink3_client.c $(LDFLAGS_STATIC)

$(SERVER_TARGET): ilink3_server.c ilink3_proto.h ilink3_sbe.h
	$(CC) $(CFLAGS) -o $@ ilink3_server.c $(LDFLAGS_DYN)

clean:
	rm -f $(CLIENT_DYN) $(CLIENT_STATIC) $(SERVER_TARGET)
