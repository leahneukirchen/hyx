
#include "common.h"
#include "blob.h"

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

void blob_init(struct blob *blob)
{
    memset(blob, 0, sizeof(*blob));
    history_init(&blob->undo);
    history_init(&blob->redo);
}

void blob_replace(struct blob *blob, size_t pos, byte const *data, size_t len, bool save_history)
{
    assert(pos + len <= blob->len);

    if (save_history) {
        history_free(&blob->redo);
        history_save(&blob->undo, REPLACE, blob, pos, len);
        ++blob->saved_dist;
    }

    if (blob->dirty)
        for (size_t i = pos / 0x1000; i < (pos + len + 0xfff) / 0x1000; ++i)
            blob->dirty[i / 8] |= 1 << i % 8;

    memcpy(blob->data + pos, data, len);
}

void blob_insert(struct blob *blob, size_t pos, byte const *data, size_t len, bool save_history)
{
    assert(pos <= blob->len);
    assert(blob_can_move(blob));
    assert(len);
    assert(!blob->dirty); /* not implemented */

    if (save_history) {
        history_free(&blob->redo);
        history_save(&blob->undo, INSERT, blob, pos, len);
        ++blob->saved_dist;
    }

    blob->data = realloc_strict(blob->data, blob->len += len);

    memmove(blob->data + pos + len, blob->data + pos, blob->len - pos - len);
    memcpy(blob->data + pos, data, len);
}

void blob_delete(struct blob *blob, size_t pos, size_t len, bool save_history)
{
    assert(pos + len <= blob->len);
    assert(blob_can_move(blob));
    assert(len);
    assert(!blob->dirty); /* not implemented */

    if (save_history) {
        history_free(&blob->redo);
        history_save(&blob->undo, DELETE, blob, pos, len);
        ++blob->saved_dist;
    }

    memmove(blob->data + pos, blob->data + pos + len, (blob->len -= len) - pos);
    blob->data = realloc_strict(blob->data, blob->len);
}

void blob_free(struct blob *blob)
{
    free(blob->filename);

    switch (blob->alloc) {
    case BLOB_MALLOC:
        free(blob->data);
        break;
    case BLOB_MMAP:
        free(blob->dirty);
        munmap_strict(blob->data, blob->len);
        break;
    }

    free(blob->clipboard.data);

    history_free(&blob->undo);
    history_free(&blob->redo);
}

bool blob_can_move(struct blob const *blob)
{
    return blob->alloc == BLOB_MALLOC;
}

bool blob_undo(struct blob *blob, size_t *pos)
{
    bool r = history_step(&blob->undo, blob, &blob->redo, pos);
    blob->saved_dist -= r;
    return r;
}

bool blob_redo(struct blob *blob, size_t *pos)
{
    bool r = history_step(&blob->redo, blob, &blob->undo, pos);
    blob->saved_dist += r;
    return r;
}

void blob_yank(struct blob *blob, size_t pos, size_t len)
{
    free(blob->clipboard.data);
    blob->clipboard.data = NULL;

    if (pos < blob_length(blob)) {
        blob->clipboard.data = malloc_strict(blob->clipboard.len = len);
        blob_read_strict(blob, pos, blob->clipboard.data, blob->clipboard.len);
    }
}

size_t blob_paste(struct blob *blob, size_t pos, enum op_type type)
{
    if (!blob->clipboard.data) return 0;

    switch (type) {
    case REPLACE:
        blob_replace(blob, pos, blob->clipboard.data, min(blob->clipboard.len, blob->len - pos), true);
        break;
    case INSERT:
        blob_insert(blob, pos, blob->clipboard.data, blob->clipboard.len, true);
        break;
    default:
        die("bad operation");
    }

    return blob->clipboard.len;
}

#define DD(F,B) (dir > 0 ? (F) : (B))

/* modified Boyer-Moore-Horspool algorithm. */
static ssize_t blob_search_range(struct blob *blob, byte const *needle, size_t len, size_t start, ssize_t end, ssize_t dir, size_t tab[256])
{
    size_t blen = blob_length(blob);

    assert(start < blen && end >= -1 && end <= (ssize_t) blen);
    assert(DD((ssize_t) start <= end, end <= (ssize_t) start));

    if (len > DD(end-start, start-end)) /* needle longer than range */
        return -1;

    for (ssize_t i = start; DD(i < end, i > end) ; ) {

        if (i + len > blen) {
            /* not enough space for pattern: skip */
            i += dir;
            continue;
        }
        assert(i >= 0 && i + len <= blen);

        bool found = true;
        for (ssize_t j = DD(len-1, 0); found && j >= 0 && (size_t) j < len; j -= dir)
            found = blob_at(blob, i + j) == needle[j];
        if (found)
            return i;

        i += dir * (ssize_t) tab[blob_at(blob, i + (dir > 0 ? len - 1 : 0))];

    }

    /* not found */
    return -1;
}

ssize_t blob_search(struct blob *blob, byte const *needle, size_t len, size_t start, ssize_t dir)
{
    size_t blen = blob_length(blob);

    if (!len || len > blen)
        return -1;

    assert(start < blen);
    assert(dir == +1 || dir == -1);

    /* could do preprocessing once per needle/dir pair, but patterns are usually short */
    size_t tab[256];
    for (size_t j = 0; j < 256; ++j)
        tab[j] = len;
    for (size_t j = 0; j < len-1; ++j)
        tab[needle[DD(j, len-1-j)]] = len-1-j;

    ssize_t r = blob_search_range(blob, needle, len, start, DD((ssize_t) blen, -1), dir, tab);
    if (r < 0)  /* wrap around */
        r = blob_search_range(blob, needle, len, DD(0, blen-1), start, dir, tab);

    return r;
}

#undef DD


/* blob_load* functions must be called with a fresh struct from blob_init() */

void blob_load(struct blob *blob, char const *filename)
{
    struct stat st;
    int fd;
    void *ptr = NULL;

    if (!filename)
        return; /* We are creating a new (still unnamed) file */

    blob->filename = strdup(filename);

    errno = 0;
    if (stat(filename, &st)) {
        if (errno != ENOENT)
            pdie("stat");
        return; /* We are creating a new file with given name */
    }

    if (0 > (fd = open(filename, O_RDONLY)))
        pdie("open");

    switch (st.st_mode & S_IFMT) {
    case S_IFREG:
        blob->len = st.st_size;
        blob->alloc = blob->len >= CONFIG_LARGE_FILESIZE ? BLOB_MMAP : BLOB_MALLOC;
        break;
    case S_IFBLK:
        blob->len = lseek_strict(fd, 0, SEEK_END);
        blob->alloc = BLOB_MMAP;
        break;
    default:
        die("unsupported file type");
    }

    if (blob->len)
        ptr = mmap_strict(NULL,
                blob->len,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_NORESERVE,
                fd,
                0);

    switch (blob->alloc) {

    case BLOB_MMAP:
        assert(ptr);
        blob->data = ptr;
        if (!(blob->dirty = calloc(((blob->len + 0xfff) / 0x1000 + 7) / 8, sizeof(*blob->dirty))))
            pdie("calloc");
        break;

    case BLOB_MALLOC:
        blob->data = malloc_strict(blob->len);
        if (ptr) {
            memcpy(blob->data, ptr, blob->len);
            munmap_strict(ptr, blob->len);
        }
        break;

    default:
        die("bad blob type");
    }

    if (close(fd))
        pdie("close");
}

void blob_load_stream(struct blob *blob, FILE *fp)
{
    const size_t alloc_size = 0x1000;
    size_t n = 0;

    while (true) {
        assert(n <= blob->len);

        if (blob->len - n < alloc_size)
            blob->data = realloc_strict(blob->data, (blob->len += alloc_size));

        size_t r = fread(blob->data + n, 1, blob->len - n, fp);
        if (!r) {
            if (feof(fp)) break;
            pdie("could not read data from stream");
        }
        n += r;
    }
    blob->data = realloc(blob->data, (blob->len = n));
}

enum blob_save_error blob_save(struct blob *blob, char const *filename)
{
    int fd;
    struct stat st;
    byte const *ptr;

    if (filename) {
        free(blob->filename);
        blob->filename = strdup(filename);
    }
    else if (blob->filename)
        filename = blob->filename;
    else
        return BLOB_SAVE_FILENAME;

    errno = 0;
    if (0 > (fd = open(filename,
                    O_WRONLY | O_CREAT,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH))) {
        switch (errno) {
        case ENOENT:  return BLOB_SAVE_NONEXISTENT;
        case EACCES:  return BLOB_SAVE_PERMISSIONS;
        case ETXTBSY: return BLOB_SAVE_BUSY;
        default: pdie("open");
        }
    }

    if (fstat(fd, &st))
        pdie("fstat");

    if ((st.st_mode & S_IFMT) == S_IFREG && ftruncate(fd, blob->len))
            pdie("ftruncate");

    for (size_t i = 0, n; i < blob->len; i += n) {

        if (blob->dirty && !(blob->dirty[i / 0x1000 / 8] & (1 << i / 0x1000 % 8))) {
            n = 0x1000 - i % 0x1000;
            continue;
        }

        ptr = blob_lookup(blob, i, &n);
        if (blob->dirty)
            n = min(0x1000 - i % 0x1000, n);

        if ((ssize_t) i != lseek(fd, i, SEEK_SET))
            pdie("lseek");

        if (0 >= (n = write(fd, ptr, n)))
            pdie("write");
    }

    if (close(fd))
        pdie("close");

    blob->saved_dist = 0;

    return BLOB_SAVE_OK;
}

bool blob_is_saved(struct blob const *blob)
{
    return !blob->saved_dist;
}

byte const *blob_lookup(struct blob const *blob, size_t pos, size_t *len)
{
    assert(pos < blob->len);

    if (len)
        *len = blob->len - pos;
    return blob->data + pos;
}

void blob_read_strict(struct blob *blob, size_t pos, byte *buf, size_t len)
{
    byte const *ptr;
    for (size_t i = 0, n; i < len; i += n) {
        ptr = blob_lookup(blob, pos, &n);
        memcpy(buf + i, ptr, (n = min(len - i, n)));
    }
}

