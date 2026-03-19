# iLink3 TCPDirect Loopback Demo — Container Environment Guide

This document explains the complete loopback setup: how two ZF stacks communicate
inside a Docker container via the TCPDirect back-to-back (b2b) software emulator,
and why each configuration knob is required.

---

## Architecture

```
┌─────────────────────────────── Docker container ────────────────────────────────┐
│                                                                                  │
│  ilink3_server (master)              ilink3_client (slave)                         │
│  ZF_ATTR: interface=b2b0            ZF_ATTR: interface=b2b1                     │
│  zftl_listen(10.0.0.1:10000)        zft_addr_bind(10.0.0.2)                     │
│         │                           zft_connect(10.0.0.1:10000)                 │
│         │                                      │                                 │
│   ┌─────▼──────────────────────────────────────▼─────┐                          │
│   │          ZF b2b emulator  (emu=1)                 │                          │
│   │   /dev/shm/zf_emu_ilink3  (~304 MB)               │                          │
│   │                                                   │                          │
│   │   b2b0 VI ◄──────── packet_memcpy ────────► b2b1 VI                         │
│   │   (TX ring, RX ring, PIO buf)   (TX ring, RX ring, PIO buf)                 │
│   └───────────────────────────────────────────────────┘                          │
│                                                                                  │
│  Backing sockets (kernel): 10.0.0.1/24 and 10.0.0.2/24 added to lo             │
└──────────────────────────────────────────────────────────────────────────────────┘
```

The server process is the **master**: it creates the shared-memory segment and starts
the emulator background thread (`zf_emu_thread`). The client process is the **slave**:
it attaches to the existing segment. All packet I/O goes through shared memory —
no kernel networking, no real NIC required.

---

## Running the loopback test

### Build the image

```bash
docker build -t ilink3-demo .
```

### Run (full loopback session)

```bash
docker run --rm \
    --shm-size=512m \
    --ulimit memlock=-1 \
    --cap-add=IPC_LOCK \
    --cap-add=NET_ADMIN \
    --privileged \
    ilink3-demo
```

The container's `CMD` is `run_loopback.sh`, which performs the complete iLink3
session (Negotiate → Establish → 3× Sequence → Terminate) and prints all
protocol events.

---

## Container flags explained

### `--shm-size=512m`

The ZF b2b emulator backs all packet buffers in a POSIX shared-memory object
(`/dev/shm/zf_emu_<shmname>`).  With `max_sbufs=128` the size breaks down as:

| Region | Size |
|--------|------|
| Packet memory (`max_sbufs × 1 MB × 2 sides`) | 256 MB |
| `emu_state` struct + descriptor rings | ~48 MB |
| **Total** | **~304 MB** |

Docker's default `/dev/shm` is **64 MB**.  Without this flag `zf_stack_alloc()`
fails immediately with `ENOMEM`.  512 MB leaves comfortable headroom.

### `--ulimit memlock=-1`

ZF must `mlock()` the packet buffer region to guarantee it is never paged out
(kernel-bypass networking reads/writes memory directly — a page fault mid-packet
would be fatal).  The default container `memlock` limit is 64 KB.  Without
`-1` (unlimited), `mlock()` returns `EPERM` and stack allocation fails.

### `--cap-add=IPC_LOCK`

Grants `CAP_IPC_LOCK`, which lifts the per-process locked-memory limit enforced
by the kernel independently of the `ulimit`.  Both `--ulimit memlock=-1` and
`--cap-add=IPC_LOCK` are needed together; either alone is insufficient.

### `--cap-add=NET_ADMIN`

`run_loopback.sh` adds two IP aliases on the loopback interface before starting
the stacks:

```bash
ip addr add 10.0.0.1/24 dev lo
ip addr add 10.0.0.2/24 dev lo
```

These addresses are used by ZF's **backing sockets** — lightweight kernel-space
TCP sockets that ZF creates in parallel with every ZF socket to handle ARP,
ICMP unreachables, and OS-level route resolution.  The backing socket for the
server binds to `10.0.0.1`; the client's to `10.0.0.2`.  `CAP_NET_ADMIN` is
required for the `ip addr add` command.

### `--privileged`

Even with `CAP_NET_ADMIN`, Docker's default seccomp profile blocks some
`netlink` operations used by `iproute2`.  `--privileged` removes the seccomp
filter and grants all capabilities.  In a hardened deployment this can be
replaced with a custom seccomp profile that whitelists the specific syscalls
(`setsockopt`, `socket(AF_NETLINK)`, `sendto`, etc.).

---

## Huge-page shim (`mmap_nohuge.so`)

### Why it is needed

ZF's `emu_alloc_huge()` requests the shared-memory region with:

```c
mmap(NULL, size, PROT_READ|PROT_WRITE,
     MAP_SHARED | MAP_HUGETLB | MAP_HUGE_2MB, fd, 0);
```

On a host without pre-allocated 2 MB huge pages this call fails with `ENOMEM`.
The shim (`mmap_nohuge.c`) intercepts `mmap` via `LD_PRELOAD`, strips
`MAP_HUGETLB` on failure, and retries with 2 MB-aligned regular pages:

```c
// On MAP_HUGETLB failure → retry without it, aligned to 2 MB
void *p = mmap_real(NULL, size + ALIGN, PROT_RW, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
// round up to 2 MB boundary, mprotect the pad regions
```

The shim is loaded for both server and client:

```bash
export LD_PRELOAD=/usr/lib/mmap_nohuge.so
```

### When it is NOT needed

Pre-allocate huge pages on the **Docker host** (not inside the container):

```bash
# On the host — before running docker
echo 256 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
```

Then pass `--device /dev/hugepages` to `docker run`.  With huge pages
available the ZF library's native `mmap` succeeds and the shim can be omitted.

---

## ZF b2b emulator attributes

```bash
ZF_ATTR="emu=1;emu_shmname=ilink3;max_sbufs=128;interface=b2b0"
```

| Attribute | Value | Meaning |
|-----------|-------|---------|
| `emu` | `1` | Back-to-back mode: two ZF stacks share a single SHM segment. Master creates it; slave attaches. |
| `emu_shmname` | `ilink3` | SHM object name: `/dev/shm/zf_emu_ilink3`. Both sides must use the same name. |
| `max_sbufs` | `128` | Number of 1 MB packet buffer super-buffers per side. Drives SHM size. |
| `interface` | `b2b0` / `b2b1` | Selects which virtual NIC inside the emulator. `b2b0` ↔ `b2b1` are the two ends of the back-to-back link. |

> **Startup order matters.**  The server (`b2b0`) must be launched first — it
> creates the SHM segment as master and sets `accept_client = true`.  The client
> (`b2b1`) waits up to 10 s for the master to be ready before failing.  The
> `sleep 1` in `run_loopback.sh` provides this gap.

---

## Why `--local-ip` is required for the emulator client

### The problem

In b2b mode, the emulator provides a **mock control plane** that bypasses the
kernel routing table.  Its path resolution function always returns:

```c
path->src = emu_conf.local_addr;   // = 0  (INADDR_ANY) in b2b mode
path->mac = dummy_mac;
path->rc  = ZF_PATH_OK;
```

When the client calls `zft_connect()` without first calling `zft_addr_bind()`,
the ZF stack constructs TCP SYN packets with source IP `0.0.0.0`.  This
triggers an assertion in the emulator's packet copy path:

```
zf_assert(((ethhdr*)src)->h_proto == zf_htons(0x0800))
where [h_proto=81] and [zf_htons(0x0800)=8]
at zf_emu.c:864
```

The assertion fires in `packet_memcpy()` inside the emu thread, which validates
that every transmitted frame is a plain IPv4 Ethernet frame (`EtherType 0x0800`)
before delivering it to the peer's RX ring.

### The fix

Calling `zft_addr_bind()` before `zft_connect()` tells the ZF stack the exact
source IP to use, overriding the `path->src = 0` returned by the mock control
plane.  With a valid source IP the ZF stack constructs a well-formed IPv4 frame
and the assertion passes.

```c
// ilink3_client.c — tcp_connect()
if (s->local_addr.sin_family == AF_INET) {
    zft_addr_bind(s->tcp_handle,
                  (struct sockaddr *)&s->local_addr,
                  sizeof(s->local_addr), 0);
}
zft_connect(s->tcp_handle, (struct sockaddr *)&s->server_addr, ...);
```

Pass it via `--local-ip`:

```bash
./ilink3_client --full-session \
    --interface b2b1 \
    --local-ip 10.0.0.2 \       # ← required in emulator
    --host 10.0.0.1 --port 10000 \
    ...
```

> **This flag is NOT needed on real Solarflare hardware.**  The real Onload
> control plane (`onload_cp_server`) performs a proper kernel route lookup and
> fills in the correct source IP automatically.

---

## Real Solarflare NIC — differences from emulator

| | Emulator (b2b, `emu=1`) | Real Solarflare NIC |
|---|---|---|
| `ZF_ATTR` | `emu=1;emu_shmname=...;interface=b2b0` | `interface=eth1` (physical NIC name) |
| `--local-ip` | **Required** (mock CP returns `0.0.0.0`) | Not needed (real CP resolves route) |
| `LD_PRELOAD` | `mmap_nohuge.so` (unless huge pages configured) | Not needed |
| `ip addr add` | Required (backing socket IPs) | Not needed (NIC has real IPs) |
| `onload_cp_server` | Not needed (emulated) | Must be running |
| Huge pages | Optional (shim covers the gap) | Recommended for best performance |
| `--shm-size` | 512m | Not needed |
| `--ulimit memlock` | `-1` | Set per production policy |

Real Solarflare invocation:

```bash
./ilink3_client --full-session \
    --interface eth1 \
    --host <CME_MSGW_IP> --port <CME_MSGW_PORT> \
    --access-key <20-char-key> \
    --secret-key <43-char-base64url> \
    --session <session-id> --firm <firm-id>
```

---

## Expected loopback output

```
[*] Starting server (b2b0, 10.0.0.1:10000)...
[*] Starting client (b2b1 → 10.0.0.1:10000)...
[TCP]       Connected to 10.0.0.1:10000
[NEGOTIATE] Sending Negotiate (tmpl=500)
[NEGOTIATE] NegotiationResponse received
[ESTABLISH] Sending Establish (tmpl=503)
[ESTABLISH] EstablishmentAck received
[SEQUENCE]  round=1  nextSeqNo=1  ↔  server_next_seq=1
[SEQUENCE]  round=2  nextSeqNo=2  ↔  server_next_seq=2
[SEQUENCE]  round=3  nextSeqNo=3  ↔  server_next_seq=3
[TERMINATE] Sending Terminate (tmpl=507)
[DONE]      Session closed cleanly.
[*] Loopback test complete.
```
