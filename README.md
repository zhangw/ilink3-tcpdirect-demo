# CME iLink3 v9 TCPDirect Demo

A small C client for connecting to CME MSGW over iLink3 v9 using TCPDirect.

Minimal demo that walks through the full iLink3 v9 session lifecycle over
TCPDirect (`zf_*` APIs) on a Solarflare NIC.

## Protocol flow

```
Client                              CME MSGW
  |                                    |
  |--- TCP SYN ----------------------->|
  |<-- TCP SYN-ACK --------------------|
  |--- TCP ACK ----------------------->|
  |                                    |
  |--- Negotiate (500) --------------->|   HMAC-SHA256 signed
  |<-- NegotiationResponse (501) ------|
  |                                    |
  |--- Establish (503) --------------->|   HMAC-SHA256 signed
  |<-- EstablishmentAck (504) ---------|
  |                                    |
  |--- Sequence (506) x3 ------------->|   keepalive / nextSeqNo
  |<-- Sequence (506) -----------------|
  |                                    |
  |--- Terminate (507) --------------->|   error_codes=0 (logout)
  |--- TCP FIN ----------------------->|
```

## Wire framing

Every iLink3 message is:

```
[SOFH 4B][SBE MessageHeader 8B][Body]

SOFH:
  uint16 msg_size        total bytes incl. SOFH
  uint16 encoding_type   0xCAFE = SBE little-endian

SBE MessageHeader:
  uint16 block_length    root block size of body
  uint16 template_id     message type (500-507)
  uint16 schema_id       always 8
  uint16 version         schema version 9
```

## Key protocol points

| Topic | Detail |
|-------|--------|
| Auth | HMAC-SHA256 over the CME canonical string for Negotiate/Establish |
| Sequence numbers | Start at 1, client and server maintain independent spaces |
| Heartbeat | `Sequence` (506) messages, not a dedicated Heartbeat message type |
| Timestamps | `uint64_t` nanoseconds since Unix epoch |
| Terminate | No ACK — send and close TCP |

## Build

```bash
# Default build (system Onload at /usr/include, /usr/lib)
make

# Or with a custom Onload installation path
make ZF_INCLUDE=/opt/onload/include ZF_LIB=/opt/onload/lib
```

Dependencies: `libonload_zf` (TCPDirect) and `libcrypto` (OpenSSL).

## Run

```bash
# TCPDirect may require elevated privileges depending on host configuration
./ilink3_client \
    --interface eth0         \
    --host <msgw_ip>         \
    --port <msgw_port>       \
    [--verbose]
```

Example:
```bash
./ilink3_client --interface eth0 --host 10.0.1.1 --port 9000 --verbose
```

Full session example:
```bash
./ilink3_client --full-session --interface eth0 --host 10.0.1.1 --port 9000 \
    --access-key ABCDEFGHIJ0123456789 --secret-key <base64url-secret> \
    --session ABC --firm XYZ
```

## Known Requirements

- Use a TCPDirect-capable interface name with `--interface`.
- Use CME-issued `access-key`, `secret-key`, `session`, and `firm` values exactly as configured.
- Only one process should actively own the same iLink3 session at a time.
- `UUID` should be a current microseconds-since-epoch value and should increase for each new negotiated session.
- `RequestTimestamp` should be current nanoseconds-since-epoch; CME documents that stale timestamps can be rejected.
- When negotiating a new UUID, reset sequence numbers to `1` in both directions as described in CME's session-layer documentation.

Reference: [CME iLink Binary Order Entry - Session Layer](https://cmegroupclientsite.atlassian.net/wiki/spaces/EPICSANDBOX/pages/714145834/iLink+Binary+Order+Entry+-+Session+Layer)

## What this demo does NOT cover

- Retransmit requests (`RetransmitRequest` 508 / `Retransmit` 509)
- Business-layer messages (order entry, mass quote, etc.)
- Primary/backup failover (`fault_tolerance_indicator`)
- Sequence gap detection
- Production-grade event loop / heartbeat timer

These are the next natural steps after this handshake demo.
