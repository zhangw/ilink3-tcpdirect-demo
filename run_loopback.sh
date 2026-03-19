#!/bin/bash
set -e

SERVER_IP="10.0.0.1"
CLIENT_IP="10.0.0.2"
PORT=10000

# ZF backing sockets bind to real kernel IPs — add aliases on the loopback
# interface so the emulator's IP addresses are reachable.
ip addr add ${SERVER_IP}/24 dev lo 2>/dev/null || true
ip addr add ${CLIENT_IP}/24 dev lo 2>/dev/null || true

# ZF_EMU_BACKTOBACK (emu=1): two ZF stacks communicate via POSIX shared memory
# named /zf_emu_<emu_shmname>. Server must start first (creates the shm as
# master); client attaches as slave and waits up to 10 s for accept_client.
# No Solarflare hardware, kernel module, or onload_cp_server required.
# max_sbufs=128 → emu_memreg_size = 128×1MB×2 = 256MB; leaves 128MB for two stack packet pools.
# shm_len ≈ 304MB → requires --shm-size=512m in docker run.
EMU_ATTR="emu=1;emu_shmname=ilink3;max_sbufs=128"

# LD_PRELOAD shim: strips MAP_HUGETLB and retries with 2MB-aligned regular
# pages when huge pages are not pre-allocated on the host.
export LD_PRELOAD=/usr/lib/mmap_nohuge.so

# ── Start server on virtual interface b2b0 ───────────────────────────────
echo "[*] Starting server (b2b0, ${SERVER_IP}:${PORT})..."
ZF_ATTR="${EMU_ATTR};interface=b2b0" \
    ./ilink3_server --interface b2b0 --ip ${SERVER_IP} --port ${PORT} --verbose &
SERVER_PID=$!
sleep 1   # wait for server to create shm + init ZF stack (accept_client=true)

# ── Run client on virtual interface b2b1 ─────────────────────────────────
# --local-ip binds the ZF socket to CLIENT_IP before zft_connect; required
# by the b2b emulator (without it the emu thread asserts on h_proto).
# Dummy credentials — server skips HMAC validation in loopback mode
echo "[*] Starting client (b2b1 → ${SERVER_IP}:${PORT})..."
ZF_ATTR="${EMU_ATTR};interface=b2b1" \
    ./ilink3_client_static --full-session \
        --interface b2b1 \
        --local-ip ${CLIENT_IP} \
        --host ${SERVER_IP} --port ${PORT} \
        --access-key AAAAAAAAAAAAAAAAAAAA \
        --secret-key AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA \
        --session AAA --firm AAAAA \
        --rounds 3 --verbose

wait ${SERVER_PID}
echo "[*] Loopback test complete."
