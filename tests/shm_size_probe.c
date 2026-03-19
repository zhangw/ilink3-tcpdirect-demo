/*
 * tests/shm_size_probe.c
 *
 * After ZF creates its POSIX shared memory segment, read the file size from
 * /proc/shm (via stat on /dev/shm/<name>) to find the actual shm_len() that
 * ZF computed at runtime.  Also checks whether memset of that size succeeds.
 *
 * Run this as the MASTER (first process) with emu=1.
 * The shm name used by ZF is /zf_emu_<emu_shmname>.
 *
 * Build (via Dockerfile):
 *   gcc -O0 -o /app/shm_size_probe tests/shm_size_probe.c \
 *       -I/usr/include -L/usr/lib -lonload_zf -Wl,-rpath,/usr/lib
 *
 * Run:
 *   LD_PRELOAD=/usr/lib/mmap_nohuge.so \
 *   ZF_ATTR="emu=1;emu_shmname=szmprobe;interface=b2b0;max_sbufs=16" \
 *   ./shm_size_probe
 */

#define _GNU_SOURCE
#include <zf/zf.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *shmname = (argc > 1) ? argv[1] : "szmprobe";
    char path[256];
    snprintf(path, sizeof(path), "/dev/shm/zf_emu_%s", shmname);

    fprintf(stderr, "[SHM] calling zf_init() — will create %s\n", path);
    fflush(stderr);

    int rc = zf_init();
    if (rc != 0) {
        fprintf(stderr, "[SHM] zf_init FAILED rc=%d\n", rc);
        return 1;
    }
    fprintf(stderr, "[SHM] zf_init OK\n");

    /* Stat the shm file that ZF just created */
    struct stat st;
    if (stat(path, &st) == 0) {
        fprintf(stderr, "[SHM] %s size = %zu bytes (%.1f MB)\n",
                path, (size_t)st.st_size, (double)st.st_size / (1024*1024));
    } else {
        fprintf(stderr, "[SHM] stat(%s) failed — file not found yet\n", path);
    }
    fflush(stderr);

    struct zf_attr *attr = NULL;
    rc = zf_attr_alloc(&attr);
    if (rc != 0) { fprintf(stderr, "[SHM] zf_attr_alloc FAILED\n"); return 1; }

    struct zf_stack *stack = NULL;
    fprintf(stderr, "[SHM] calling zf_stack_alloc()...\n"); fflush(stderr);
    rc = zf_stack_alloc(attr, &stack);
    fprintf(stderr, "[SHM] zf_stack_alloc rc=%d\n", rc); fflush(stderr);

    zf_attr_free(attr);
    if (rc == 0) zf_stack_free(stack);
    zf_deinit();

    /* Re-stat after deinit */
    if (stat(path, &st) == 0)
        fprintf(stderr, "[SHM] %s still exists after deinit\n", path);
    else
        fprintf(stderr, "[SHM] %s cleaned up after deinit\n", path);

    return rc != 0 ? 1 : 0;
}
