#ifndef TERM_H
#define TERM_H

#include <stdbool.h>
#include <termios.h>

struct term
{
    bool initialized;

    bool is_basic;  /* no scrolling, limited formatting */

    struct termios attrs;

    bool winch;
    bool tstp, cont;
};

extern struct term term;  /* term.c */

void term_init(void);
void term_text(bool leave_alternate);
void term_visual(void);
void term_size(unsigned *width, unsigned *height);

#endif
