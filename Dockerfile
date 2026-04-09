# Dockerfile — iLink3 demo + test suite
#
# Uses pre-built base image with OpenOnload + TCPDirect already compiled.
# The base image is built from Dockerfile.base and pushed to ghcr.io.
#
# For local builds with deps/ available, use Dockerfile.full instead.

ARG BASE_IMAGE=ghcr.io/zhangw/ilink3-base:latest
FROM ${BASE_IMAGE}

WORKDIR /app
COPY . .
RUN make all

# Build huge-page fallback shim (needed for ZF_DEVEL emulator without pre-allocated hugepages)
RUN gcc -shared -fPIC -O2 -o /usr/lib/mmap_nohuge.so mmap_nohuge.c -ldl

# Build diagnostic test probes
RUN gcc -O0 -o /app/zf_b2b_probe     tests/zf_b2b_probe.c     -I/usr/include -L/usr/lib -lonload_zf -Wl,-rpath,/usr/lib
RUN gcc -O0 -o /app/shm_size_probe   tests/shm_size_probe.c   -I/usr/include -L/usr/lib -lonload_zf -Wl,-rpath,/usr/lib

# Build test suite
RUN make test

RUN mkdir -p /app/tests/results
RUN chmod +x /app/run_loopback.sh /app/tests/run_all_tests.sh \
             /app/tests/integration/run_integration.sh \
             /app/tests/perf/run_perf.sh

CMD ["/app/run_loopback.sh"]
