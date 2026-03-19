/*
 * tests/zf_b2b_probe.c
 *
 * Step-by-step ZF back-to-back emulator init probe.
 * Prints a checkpoint before each ZF call so we can see exactly
 * where a crash (SIGBUS/SIGSEGV) occurs.
 *
 * Build (via Dockerfile):
 *   gcc -O0 -o /app/zf_b2b_probe tests/zf_b2b_probe.c \
 *       -I/usr/include -L/usr/lib -lonload_zf -Wl,-rpath,/usr/lib
 *
 * Run:
 *   LD_PRELOAD=/usr/lib/mmap_nohuge.so \
 *   ZF_ATTR="emu=1;emu_shmname=probe1;interface=b2b0;max_sbufs=16" \
 *   ./zf_b2b_probe
 */

#include <zf/zf.h>
#include <zf/zf_tcp.h>
#include <stdio.h>
#include <stdlib.h>

#define STEP(n, msg) do { fprintf(stderr, "[PROBE] step %d: %s\n", n, msg); fflush(stderr); } while(0)
#define CHECK(rc, fn) do { \
    if ((rc) != 0) { fprintf(stderr, "[PROBE] " fn " FAILED rc=%d\n", rc); exit(1); } \
    fprintf(stderr, "[PROBE] " fn " OK\n"); fflush(stderr); \
} while(0)

int main(void)
{
    int rc;
    struct zf_attr  *attr = NULL;
    struct zf_stack *stack = NULL;
    struct zf_muxer_set *muxer = NULL;

    STEP(1, "calling zf_init()");
    rc = zf_init();
    CHECK(rc, "zf_init");

    STEP(2, "calling zf_attr_alloc()");
    rc = zf_attr_alloc(&attr);
    CHECK(rc, "zf_attr_alloc");

    STEP(3, "calling zf_stack_alloc()");
    rc = zf_stack_alloc(attr, &stack);
    CHECK(rc, "zf_stack_alloc");

    STEP(4, "calling zf_muxer_alloc()");
    rc = zf_muxer_alloc(stack, &muxer);
    CHECK(rc, "zf_muxer_alloc");

    STEP(5, "init complete — freeing resources");
    zf_muxer_free(muxer);
    zf_stack_free(stack);
    zf_attr_free(attr);
    zf_deinit();

    fprintf(stderr, "[PROBE] all OK\n");
    return 0;
}
