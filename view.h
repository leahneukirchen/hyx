#ifndef VIEW_H
#define VIEW_H

#define _GNU_SOURCE

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <setjmp.h>

struct input;

struct view {
    struct blob *blob;
    struct input *input; /* FIXME hack */

    size_t start;

    uint8_t *dirty;
    signed scroll;

    unsigned width, height; /* terminal dimensions */

    bool cols_fixed;
    unsigned rows, cols; /* bytes currently in view */
    unsigned pos_digits;
    bool color;
};

void view_init(struct view *view, struct blob *blob, struct input *input);
void view_recompute(struct view *view, bool changed);
void view_set_cols(struct view *view, bool relative, int cols);
void view_free(struct view *view);

void view_message(struct view *view, char const *msg, char const *color);
void view_error(struct view *view, char const *msg);

void view_update(struct view *view);

void view_dirty_at(struct view *view, size_t pos);
void view_dirty_from(struct view *view, size_t from);
void view_dirty_fromto(struct view *view, size_t from, size_t to);
void view_adjust(struct view *view);

#endif
