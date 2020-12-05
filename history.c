
#include "common.h"
#include "history.h"
#include "blob.h"

#include <string.h>

struct diff {
    enum op_type type;
    size_t pos;
    byte *data;
    size_t len;
    struct diff *next;
};

static void diff_apply(struct blob *blob, struct diff *diff)
{
    switch (diff->type) {
    case REPLACE:
        blob_replace(blob, diff->pos, diff->data, diff->len, false);
        break;
    case INSERT:
        blob_insert(blob, diff->pos, diff->data, diff->len, false);
        break;
    case DELETE:
        blob_delete(blob, diff->pos, diff->len, false);
        break;
    default:
        die("unknown operation");
    }
}

void history_init(struct diff **history)
{
    *history = NULL;
}

void history_free(struct diff **history)
{
    struct diff *tmp, *cur = *history;
    while (cur) {
        tmp = cur;
        cur = cur->next;
        free(tmp->data);
        free(tmp);
    }
    *history = NULL;
}

/* pushes a diff that _undoes_ the passed operation */
void history_save(struct diff **history, enum op_type type, struct blob *blob, size_t pos, size_t len)
{
    struct diff *diff = malloc_strict(sizeof(*diff));
    diff->type = type;
    diff->pos = pos;
    diff->len = len;
    diff->next = *history;

    switch (type) {
    case DELETE:
        diff->type = INSERT;
        /* fall-through */
    case REPLACE:
        blob_read_strict(blob, pos, diff->data = malloc_strict(len), len);
        break;
    case INSERT:
        diff->type = DELETE;
        diff->data = NULL;
        break;
    default:
        die("unknown operation");
    }

    *history = diff;
}

bool history_step(struct diff **from, struct blob *blob, struct diff **to, size_t *pos)
{
    struct diff *diff = *from;

    if (!diff)
        return false;

    if (pos)
        *pos = diff->pos;

    if (to)
        history_save(to, diff->type, blob, diff->pos, diff->len);

    *from = diff->next;
    diff_apply(blob, diff);
    free(diff->data);
    free(diff);

    return true;
}

