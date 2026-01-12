
#include "history.h"

#include "common.h"
#include "blob.h"

struct change {
    enum change_type type;
    size_t pos;
    byte *data;
    size_t len;
    struct change *next;
};

static void change_apply(struct blob *blob, struct change *change)
{
    switch (change->type) {
    case REPLACE:
        blob_replace(blob, change->pos, change->data, change->len, false);
        break;
    case INSERT:
        blob_insert(blob, change->pos, change->data, change->len, false);
        break;
    case DELETE:
        blob_delete(blob, change->pos, change->len, false);
        break;
    default:
        die("unknown operation");
    }
}

void history_init(struct change **history)
{
    *history = NULL;
}

void history_free(struct change **history)
{
    struct change *tmp, *cur = *history;
    while (cur) {
        tmp = cur;
        cur = cur->next;
        free(tmp->data);
        free(tmp);
    }
    *history = NULL;
}

/* pushes a change that _undoes_ the passed operation */
void history_save(struct change **history, enum change_type type, struct blob *blob, size_t pos, size_t len)
{
    struct change *change = malloc_strict(sizeof(*change));
    change->type = type;
    change->pos = pos;
    change->len = len;
    change->next = *history;

    switch (type) {
    case DELETE:
        change->type = INSERT;
        /* fall-through */
    case REPLACE:
        blob_read_strict(blob, pos, change->data = malloc_strict(len), len);
        break;
    case INSERT:
        change->type = DELETE;
        change->data = NULL;
        break;
    default:
        die("unknown operation");
    }

    *history = change;
}

bool history_step(struct change **from, struct blob *blob, struct change **to, size_t *pos)
{
    struct change *change = *from;

    if (!change)
        return false;

    if (pos)
        *pos = change->pos;

    if (to)
        history_save(to, change->type, blob, change->pos, change->len);

    *from = change->next;
    change_apply(blob, change);
    free(change->data);
    free(change);

    return true;
}

