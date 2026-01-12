
#define _GNU_SOURCE

#include "term.h"

#include <assert.h>
#include <sys/ioctl.h>

#include "ansi.h"
#include "common.h"
#include "view.h"

struct term term;

void term_init(void)
{
    if (tcgetattr(fileno(stdin), &term.attrs))
        pdie("tcgetattr");

    char const *name = getenv("TERM");
    term.is_basic = name && !strcmp(name, "Linux");  /* Linux vconsole */

    term.initialized = true;
}

void term_text(bool leave_alternate)
{
    if (!term.initialized)
        return;

    if (leave_alternate)
        fputs(leave_alternate_screen, stdout);
    cursor_column(0);
    fputs(clear_line, stdout);
    fputs(show_cursor, stdout);
    fflush(stdout);

    if (tcsetattr(fileno(stdin), TCSANOW, &term.attrs)) {
        /* can't pdie() because that risks infinite recursion */
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
}

void term_visual(void)
{
    assert(term.initialized);

    struct termios attrs = term.attrs;
    attrs.c_lflag &= ~ICANON & ~ECHO;
    if (tcsetattr(fileno(stdin), TCSANOW, &attrs))
        pdie("tcsetattr");

    fputs(enter_alternate_screen, stdout);
    fputs(hide_cursor, stdout);
    fflush(stdout);
}

void term_size(unsigned *width, unsigned *height)
{
    struct winsize winsz;
    if (-1 == ioctl(fileno(stdout), TIOCGWINSZ, &winsz))
        pdie("ioctl");

    if (width)
        *width = winsz.ws_col;
    if (height)
        *height = winsz.ws_row;
}


/* common.h */
void die(char const *s)
{
    term_text(true);
    fprintf(stderr, "%s\n", s);
    exit(EXIT_FAILURE);
}

/* common.h */
void pdie(char const *s)
{
    term_text(true);
    perror(s);
    exit(EXIT_FAILURE);
}
