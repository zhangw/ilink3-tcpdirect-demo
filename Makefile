CC     = gcc
CFLAGS = -O2 -Wall -Wextra -std=gnu11

# Adjust these to your TCPDirect installation
ZF_INCLUDE ?= include
ZF_LIB     ?= lib

TARGET = ilink3_demo
SRC    = ilink3_demo.c

CFLAGS += -I$(ZF_INCLUDE)

# ── Platform detection ────────────────────────────────────────────────────
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  # macOS host → cross-compile to linux/x86_64 via zig cc
  # Requires: brew install zig
  # Requires: Linux x86_64 libcrypto.a in $(ZF_LIB) (e.g. from openssl-devel)
  CC      = zig cc -target x86_64-linux-gnu
  LDFLAGS = $(ZF_LIB)/libonload_zf_static.a $(ZF_LIB)/libcrypto.a -lpthread -ldl
else
  # Linux host → native gcc build
  LDFLAGS = -L$(ZF_LIB) -Wl,-rpath,$(ZF_LIB) -lonload_zf -lcrypto
endif

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)
