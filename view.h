#ifndef VIEW_H
#define VIEW_H

#include "blob.h"

#include <termios.h>
#include <sys/ioctl.h>

struct input;
struct view {
    bool initialized;

    struct blob *blob;
    struct input *input; /* FIXME hack */

    size_t start;

    uint8_t *dirty;
    signed scroll;

    bool cols_fixed;
    unsigned rows, cols;
    unsigned pos_digits;
    bool color;
    bool winch;
    bool tstp, cont;

    struct termios term;
};

void view_init(struct view *view, struct blob *blob, struct input *input);
void view_text(struct view *view, bool leave_alternate);
void view_visual(struct view *view);
void view_recompute(struct view *view, bool winch);
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
