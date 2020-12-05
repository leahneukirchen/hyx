
#include "common.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>

unsigned long bit_length(unsigned long n)
{
    unsigned long r = 0;
    do ++r; while (n >>= 1);
    return r;
}


void *malloc_strict(size_t len)
{
    void *ptr;
    errno = 0;
    if (!(ptr = malloc(len)) && errno)
        pdie("malloc");
    return ptr;
}

void *realloc_strict(void *ptr, size_t len)
{
    errno = 0;
    if (!(ptr = realloc(ptr, len)) && errno)
        pdie("realloc");
    return ptr;
}

void *mmap_strict(void *addr, size_t len, int prot, int flags, int fildes, off_t off)
{
    void *ptr;
    if (MAP_FAILED == (ptr = mmap(addr, len, prot, flags, fildes, off)))
        pdie("mmap");
    return ptr;
}

void munmap_strict(void *addr, size_t len)
{
    if (munmap(addr, len))
        pdie("munmap");
}

off_t lseek_strict(int fildes, off_t offset, int whence)
{
    off_t ret;
    if (0 >= (ret = lseek(fildes, offset, whence)))
        pdie("lseek");
    return ret;
}

char *fgets_retry(char *s, int size, FILE *stream)
{
    char *ret;
retry:
    errno = 0;
    if (!(ret = fgets(s, size, stream))) {
        if (errno == EINTR)
            goto retry;
        pdie("fgets");
    }
    return ret;
}

uint64_t monotonic_microtime()
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t))
        pdie("clock_gettime");
    return (uint64_t) t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

