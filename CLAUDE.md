# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a **CME iLink3 v9 Protocol Demo** — a minimal C client demonstrating the iLink3 session lifecycle (Negotiate → Establish → Sequence/Heartbeat → Terminate) over TCPDirect (high-performance TCP for Solarflare NICs).

## Build Commands

```bash
# Default build (system Onload installation at /usr/include, /usr/lib)
make

# Build with custom Onload installation path
make ZF_INCLUDE=/opt/onload/include ZF_LIB=/opt/onload/lib

# Clean
make clean
```

**Compiler:** GCC with `-O2 -Wall -Wextra -std=gnu11`
**Dependencies:** `libonload_zf` (TCPDirect) and `libcrypto` for SHA-256/HMAC

## Running

```bash
# TCPDirect may require elevated privileges depending on host configuration
./ilink3_client \
    --interface eth0       \
    --host <ip>              # CME MSGW IP
    --port <port>            # CME MSGW port
    [--verbose]              # detailed logging
```

```bash
# Full iLink3 session
./ilink3_client --full-session \
    --interface eth0         \
    --host <ip>              # CME MSGW IP
    --port <port>            # CME MSGW port
    --access-key <20chars>   # iLink3 access key
    --secret-key <43chars>   # Base64URL secret key (from CME)
    --session <str>          # CME Session ID (up to 3 chars)
    --firm <str>             # CME Firm ID (up to 5 chars)
    [--rounds N]             # keepalive rounds (default: 3)
    [--app-name <str>]       # TradingSystemName (default: demo)
    [--app-ver <str>]        # TradingSystemVersion (default: 1.0)
    [--app-vendor <str>]     # TradingSystemVendor (default: demo)
```

No automated tests exist. Verification is done by running against a CME test gateway.

## Architecture

### Key Files

- **`ilink3_client.c`** — Client binary; full session state machine (TCP connect → Negotiate → Establish → Sequence → Terminate)
- **`ilink3_server.c`** — Loopback server binary; handles the server side of the iLink3 session lifecycle
- **`ilink3_proto.h`** — Shared transport utilities (ZF stack init, send/recv helpers, frame extraction); included by both client and server
- **`ilink3_sbe.h`** — SBE wire format structs (`sofh_t`, `sbe_header_t`, message bodies), all `__attribute__((packed))`; helper functions for framing/parsing
- **`ilinkbinary.xml`** — Full CME SBE schema (reference only; not used at runtime)

### Session State Machine

```
S_IDLE → S_TCP_CONNECTING → S_TCP_CONNECTED → S_NEGOTIATING
       → S_NEGOTIATED → S_ESTABLISHING → S_ACTIVE → S_TERMINATING → S_CLOSED
```

### Protocol Phases & Message Templates

| Phase | Client→Server | Server→Client |
|-------|---------------|---------------|
| Negotiate | 500 | 501 (or reject) |
| Establish | 503 | 504 (or reject) |
| Keepalive | 506 (Sequence) | 506 (Sequence) |
| Terminate | 507 | — (no ACK) |

### Message Framing

Every message is: **SOFH (4 bytes)** + **SBE MessageHeader (8 bytes)** + **body**

- SOFH: 2-byte little-endian total frame length plus 2-byte encoding type `0xCAFE`
- SBE header: block length, template ID, schema ID (8), version (9)

### Authentication

HMAC-SHA256 ("CME-1-SHA-256") with Base64URL-decoded secret key. Canonical strings:

- **Negotiate**: `str(ts) + "\n" + str(uuid) + "\n" + session + "\n" + firm`
- **Establish**: above + `"\n" + ts_name + "\n" + ts_ver + "\n" + ts_vendor + "\n" + str(next_seq_no) + "\n" + str(keep_alive_interval)`

Signature is 32 raw binary bytes placed in the `HMACSignature` field. Both `--session` and `--firm` are part of the HMAC input and must match the values registered with CME.

### TCPDirect (Onload/ZF) Usage

- `zf_stack` — per-process network stack bound to a NIC interface via `--interface <iface>`
- `zf_muxer_set` — event multiplexer for the socket
- `zf_reactor_perform()` — must be called in polling loops to drive the stack
- `zf_muxer_wait()` — blocks until socket is readable/writable (with ns-resolution timeout)

### Tunable Constants (top of `ilink3_client.c`)

| Constant | Default | Purpose |
|----------|---------|---------|
| `RX_BUF_LEN` / `TX_BUF_LEN` | 4096 | I/O buffer sizes |
| `POLL_TIMEOUT_NS` | 100ms | Reactor polling interval |
| `HEARTBEAT_SECS` | 10 | Heartbeat interval sent to server |
| `KEEPALIVE_ROUNDS` | 3 | Default Sequence loop iterations |
| `CONNECT_TIMEOUT_S` | 5 | TCP connection timeout |
