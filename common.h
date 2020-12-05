#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>


/* round columns to a multiple of this */
#define CONFIG_ROUND_COLS 0x8

/* mmap files larger than this */
#define CONFIG_LARGE_FILESIZE (256 * (1 << 20)) /* 256 megabytes */

/* microseconds to wait for the rest of what could be an escape sequence */
#define CONFIG_WAIT_ESCAPE (10000) /* 10 milliseconds */


typedef uint8_t byte;

void die(char const *s) __attribute__((noreturn)); /* hyx.c */
void pdie(char const *s) __attribute__((noreturn)); /* hyx.c */

static inline size_t min(size_t x, size_t y)
    { return x < y ? x : y; }
static inline size_t max(size_t x, size_t y)
    { return x > y ? x : y; }
static inline size_t absdiff(size_t x, size_t y)
    { return x > y ? x - y : y - x; }

unsigned long bit_length(unsigned long n);

void *malloc_strict(size_t len);
void *realloc_strict(void *ptr, size_t len);

void *mmap_strict(void *addr, size_t len, int prot, int flags, int fildes, off_t off);
void munmap_strict(void *addr, size_t len);

off_t lseek_strict(int fildes, off_t offset, int whence);

char *fgets_retry(char *s, int size, FILE *stream);

uint64_t monotonic_microtime();

#endif
