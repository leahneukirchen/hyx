#ifndef BLOB_H
#define BLOB_H

#include "common.h"
#include "history.h"

enum blob_alloc {
    BLOB_MALLOC = 0,
    BLOB_MMAP,
};

struct blob {
    enum blob_alloc alloc;

    size_t len;
    byte *data;

    char *filename;

    uint8_t *dirty;

    struct diff *undo, *redo;
    ssize_t saved_dist;

    struct {
        size_t len;
        byte *data;
    } clipboard;
};

void blob_init(struct blob *blob);
void blob_replace(struct blob *blob, size_t pos, byte const *data, size_t len, bool save_history);
void blob_insert(struct blob *blob, size_t pos, byte const *data, size_t len, bool save_history);
void blob_delete(struct blob *blob, size_t pos, size_t len, bool save_history);
void blob_free(struct blob *blob);

bool blob_can_move(struct blob const *blob);

bool blob_undo(struct blob *blob, size_t *pos);
bool blob_redo(struct blob *blob, size_t *pos);

void blob_yank(struct blob *blob, size_t pos, size_t len);
size_t blob_paste(struct blob *blob, size_t pos, enum op_type type);

ssize_t blob_search(struct blob *blob, byte const *needle, size_t len, size_t start, ssize_t dir);

void blob_load(struct blob *blob, char const *filename);
void blob_load_stream(struct blob *blob, FILE *fp);
enum blob_save_error {
    BLOB_SAVE_OK = 0,
    BLOB_SAVE_FILENAME,
    BLOB_SAVE_NONEXISTENT,
    BLOB_SAVE_PERMISSIONS,
    BLOB_SAVE_BUSY,
} blob_save(struct blob *blob, char const *filename);
bool blob_is_saved(struct blob const *blob);

static inline size_t blob_length(struct blob const *blob)
    { return blob->len; }
byte const *blob_lookup(struct blob const *blob, size_t pos, size_t *len);
static inline byte blob_at(struct blob const *blob, size_t pos)
    { return *blob_lookup(blob, pos, NULL); }
void blob_read_strict(struct blob *blob, size_t pos, byte *buf, size_t len);

#endif
