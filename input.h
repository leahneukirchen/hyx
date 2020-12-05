#ifndef INPUT_H
#define INPUT_H

#include "view.h"

struct input {
    struct view *view;

    enum mode {
        INPUT,
        SELECT,
    } mode;
    struct {
        bool insert: 1;
        bool ascii: 1;
    } input_mode;

    size_t cur, sel;
    bool low_nibble;
    byte cur_val;

    struct {
        size_t len;
        byte *needle;
    } search;

    bool quit;
};

void input_init(struct input *input, struct view *view);
void input_free(struct input *input);

void input_get(struct input *input, bool *quit);

#endif
