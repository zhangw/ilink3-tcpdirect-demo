# CME iLink3 v9 TCPDirect Demo

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
# Adjust paths for your TCPDirect installation
make ZF_INCLUDE=/opt/onload/include ZF_LIB=/opt/onload/lib
```

Dependencies: `libonload_zf`, `libssl`, `libcrypto` (OpenSSL).

## Run

```bash
# Must run as root (or with CAP_NET_RAW) for TCPDirect
sudo ./ilink3_demo \
    --interface eth0         \
    --host <msgw_ip>         \
    --port <msgw_port>       \
    --access-key <access_key_id> \
    --secret-key <hmac_secret_b64url> \
    --session <session_id>   \
    --firm <firm_id>
```

Example:
```bash
sudo ./ilink3_demo --interface eth0 --host 10.0.1.1 --port 9000 \
    --access-key ABCDEFGHIJ0123456789 --secret-key <secret> \
    --session ABC --firm XYZ
```

## What this demo does NOT cover

- Retransmit requests (`RetransmitRequest` 508 / `Retransmit` 509)
- Business-layer messages (order entry, mass quote, etc.)
- Primary/backup failover (`fault_tolerance_indicator`)
- Sequence gap detection
- Production-grade event loop / heartbeat timer

These are the next natural steps after this handshake demo.
