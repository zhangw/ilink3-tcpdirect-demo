/*
 * mmap_nohuge.c — LD_PRELOAD shim for ZF_DEVEL emulator on systems without
 * pre-allocated 2 MB huge pages.
 *
 * TCPDirect's __alloc_huge() calls:
 *   mmap(NULL, size, PROT_RW, MAP_ANONYMOUS|MAP_PRIVATE|MAP_HUGETLB|MAP_HUGE_2MB, -1, 0)
 *
 * When huge pages are unavailable this fails with ENOMEM.  We intercept the
 * call and retry with regular pages aligned to 2 MB (by over-allocating and
 * trimming), which satisfies TCPDirect's alignment assertions.
 *
 * Build (Dockerfile):
 *   gcc -shared -fPIC -O2 -o /usr/lib/mmap_nohuge.so mmap_nohuge.c -ldl
 *
 * Usage:
 *   LD_PRELOAD=/usr/lib/mmap_nohuge.so ./ilink3_server ...
 */

#define _GNU_SOURCE
#include <sys/mman.h>
#include <dlfcn.h>
#include <stdint.h>
#include <unistd.h>

#ifndef MAP_HUGETLB
#define MAP_HUGETLB 0x40000
#endif

/* Strip MAP_HUGETLB and MAP_HUGE_* size bits (bits 26-31). */
#define HUGE_FLAG_MASK  (MAP_HUGETLB | (0x3f << 26))

#define HUGE_PAGE_SIZE  (2UL * 1024 * 1024)   /* 2 MiB */

typedef void *(*mmap_fn_t)(void *, size_t, int, int, int, off_t);

static mmap_fn_t get_real_mmap(void)
{
    static mmap_fn_t fn;
    if (!fn)
        fn = (mmap_fn_t)dlsym(RTLD_NEXT, "mmap");
    return fn;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    mmap_fn_t real = get_real_mmap();
    void *ptr = real(addr, length, prot, flags, fd, offset);

    if (ptr != MAP_FAILED || !(flags & MAP_HUGETLB))
        return ptr;

    /* Huge pages unavailable — fall back to regular pages with 2 MB alignment.
     * Over-allocate by (HUGE_PAGE_SIZE - 1) bytes, then trim the leading
     * unaligned region so the returned pointer is 2 MB-aligned. */
    int plain_flags = flags & ~HUGE_FLAG_MASK;
    size_t over = length + HUGE_PAGE_SIZE - 1;
    void *raw = real(NULL, over, prot, plain_flags, fd, offset);
    if (raw == MAP_FAILED)
        return MAP_FAILED;

    uintptr_t base  = (uintptr_t)raw;
    uintptr_t aligned = (base + HUGE_PAGE_SIZE - 1) & ~(HUGE_PAGE_SIZE - 1);
    size_t trim = aligned - base;
    if (trim > 0)
        munmap(raw, trim);

    return (void *)aligned;
}
