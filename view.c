
#include "common.h"
#include "blob.h"
#include "view.h"
#include "input.h"
#include "ansi.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static void fprint(FILE *fp, char const *s) { fprintf(fp, "%s", s); }
static void print(char const *s) { fprint(stdout, s); }

static size_t view_end(struct view const *view)
{
    return view->start + view->rows * view->cols;
}

void view_init(struct view *view, struct blob *blob, struct input *input)
{
    memset(view, 0, sizeof(*view));
    view->blob = blob;
    view->input = input;
    view->pos_digits = 4; /* rather arbitrary */
    view->color = true;
    if (tcgetattr(fileno(stdin), &view->term))
        pdie("tcgetattr");
    view->initialized = true;
}

void view_text(struct view *view)
{
    if (!view->initialized) return;

    cursor_column(0);
    print(clear_line);
    print(show_cursor);

    if (tcsetattr(fileno(stdin), TCSANOW, &view->term)) {
        /* can't pdie() because that risks infinite recursion */
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
}

void view_visual(struct view *view)
{
    assert(view->initialized);

    struct termios term = view->term;
    term.c_lflag &= ~ICANON & ~ECHO;
    if (tcsetattr(fileno(stdin), TCSANOW, &term))
        pdie("tcsetattr");

    print(hide_cursor);
}

void view_set_cols(struct view *view, bool relative, int cols)
{
    if (relative) {
        if (cols >= 0 || (unsigned) -cols <= view->cols)
            cols += view->cols;
        else
            cols = view->cols;
    }

    if (!cols) {
        if (view->cols_fixed) {
            view->cols_fixed = false;
            view_recompute(view, true);
        }
        return;
    }

    view->cols_fixed = true;

    if ((unsigned) cols != view->cols) {
        view->cols = cols;
        view_dirty_from(view, 0);
    }
}

void view_recompute(struct view *view, bool winch)
{
    struct winsize winsz;
    unsigned old_rows = view->rows, old_cols = view->cols;
    unsigned digs = (bit_length(max(2, blob_length(view->blob)) - 1) + 3) / 4;

    if (digs > view->pos_digits) {
        view->pos_digits = digs;
        view_dirty_from(view, 0);
    }
    else if (!winch)
        return;

    if (-1 == ioctl(fileno(stdout), TIOCGWINSZ, &winsz))
        pdie("ioctl");

    view->rows = winsz.ws_row;
    if (!view->cols_fixed) {
        view->cols = (winsz.ws_col - (view->pos_digits + strlen(": ") + strlen("||"))) / strlen("xx c");

        if (view->cols > CONFIG_ROUND_COLS)
            view->cols -= view->cols % CONFIG_ROUND_COLS;
    }

    if (!view->rows || !view->cols)
        die("window too small.");

    if (view->rows == old_rows && view->cols == old_cols)
        return;

    /* update dirtiness array */
    if ((view->dirty = realloc_strict(view->dirty, view->rows * sizeof(*view->dirty))))
        memset(view->dirty, 0, view->rows * sizeof(*view->dirty));
    view_dirty_from(view, 0);

    view_adjust(view);

    print(clear_screen);
}

void view_free(struct view *view)
{
    free(view->dirty);
}

void view_message(struct view *view, char const *msg, char const *color)
{
    cursor_line(view->rows - 1);
    print(clear_line);
    if (view->color && color) print(color);
    printf("%*c  %s", view->pos_digits, ' ', msg);
    if (view->color && color) print(color_normal);
    fflush(stdout);
    view->dirty[view->rows - 1] = 2; /* redraw at the next keypress */
}

void view_error(struct view *view, char const *msg)
{
    view_message(view, msg, color_red);
}

/* FIXME hex and ascii mode look very similar */
static void render_line(struct view *view, size_t off, size_t last)
{
    byte b;
    char digits[0x10], *asciiptr;
    size_t asciilen;
    FILE *asciifp;
    struct input *I = view->input;

    size_t sel_start = min(I->cur, I->sel), sel_end = max(I->cur, I->sel);
    char const *last_color = NULL, *next_color;

    if (!(asciifp = open_memstream(&asciiptr, &asciilen)))
        pdie("open_memstream");
#define BOTH(EX) for (FILE *fp; ; ) { fp = stdout; EX; fp = asciifp; EX; break; }

    if (off <= I->cur && I->cur < off + view->cols) {
        /* cursor in current line */
        if (view->color) print(color_yellow);
        printf("%0*zx%c ", view->pos_digits, I->cur, I->input_mode.insert ? '+' : '>');
        if (view->color) print(color_normal);
    }
    else {
        printf("%0*zx: ", view->pos_digits, off);
    }

    if (I->mode == SELECT && off > sel_start && off <= sel_end)
        print(underline_on);

    for (size_t j = 0, len = blob_length(view->blob); j < view->cols; ++j) {

        if (off + j < len) {
            sprintf(digits, "%02hhx", b = blob_at(view->blob, off + j));
        }
        else {
            b = 0;
            strcpy(digits, "  ");
        }

        if (I->mode == SELECT && off + j == sel_start)
            print(underline_on);

        if (off + j >= last) {
            for (size_t p = j; p < view->cols; ++p)
                printf("   ");
            break;
        }

        if (off + j == I->cur) {
            next_color = I->cur >= blob_length(view->blob) ? color_red : color_yellow;
            BOTH(
                if (view->color && next_color != last_color) fprint(fp, next_color);
                fprint(fp, inverse_video_on);
            );

            if (!I->input_mode.ascii) {
                print(bold_on);
                if (I->mode == INPUT && !I->low_nibble) print(underline_on);
                putchar(digits[0]);
                if (I->mode == INPUT) print(I->low_nibble ? underline_on : underline_off);
                putchar(digits[1]);
                if (I->mode == INPUT && I->low_nibble) print(underline_off);
                print(bold_off);
            }
            else
                printf("%s", digits);

            if (I->mode == INPUT && I->input_mode.ascii)
                fprintf(asciifp, "%s%s%c%s%s", bold_on, underline_on, isprint(b) ? b : '.', underline_off, bold_off);
            else
                fputc(isprint(b) ? b : '.', asciifp);

            BOTH(
                fprint(fp, inverse_video_off);
            );
        }
        else {
            next_color = isalnum(b) ? color_cyan
                       : isprint(b) ? color_blue
                       : !b ? color_red
                       : color_normal;
            BOTH(
                if (view->color && next_color != last_color)
                    fprint(fp, next_color);
            );
            fputc(isprint(b) ? b : '.', asciifp);
            printf("%s", digits);
        }
        last_color = next_color;

        if (I->mode == SELECT && (off + j == sel_end || j == view->cols - 1))
            print(underline_off);

        putchar(' ');
    }
    if (view->color) print(color_normal);

#undef BOTH
    if (fclose(asciifp))
        pdie("fclose");
    putchar('|');
    printf("%s", asciiptr);
    free(asciiptr);
    if (view->color) print(color_normal);
    putchar('|');
}

void view_update(struct view *view)
{
    size_t last = max(blob_length(view->blob), view->input->cur + 1);

    if (view->scroll) {
        printf("\x1b[%ld%c", labs(view->scroll), view->scroll > 0 ? 'S' : 'T');
        view->scroll = 0;
    }

    for (size_t i = view->start, l = 0; i < view_end(view); i += view->cols, ++l) {
        /* dirtiness counter enables displaying messages until keypressed; */
        if (!view->dirty[l] || --view->dirty[l])
            continue;
        cursor_line(l);
        print(clear_line);
        if (i < last)
            render_line(view, i, last);
    }

    fflush(stdout);
}

void view_dirty_at(struct view *view, size_t pos)
{
    view_dirty_fromto(view, pos, pos + 1);
}

void view_dirty_from(struct view *view, size_t from)
{
    view_dirty_fromto(view, from, view_end(view));
}

void view_dirty_fromto(struct view *view, size_t from, size_t to)
{
    size_t lfrom, lto;
    from = max(view->start, from);
    to = min(view_end(view), to);
    if (from < to) {
        lfrom = (from - view->start) / view->cols;
        lto = (to - view->start + view->cols - 1) / view->cols;
        for (size_t i = lfrom; i < lto; ++i)
            view->dirty[i] = max(view->dirty[i], 1);
    }
}

static size_t satadd(size_t x, size_t y, size_t b)
    { assert(b >= 1); return min(b - 1, x + y); }
static size_t satsub(size_t x, size_t y, size_t a)
    { return x < y || x - y < a ? a : x - y; }

void view_adjust(struct view *view)
{
    size_t old_start = view->start;

    assert(view->input->cur <= blob_length(view->blob));

    if (view->input->cur >= view_end(view))
        view->start = satadd(view->start,
                (view->input->cur + 1 - view_end(view) + view->cols - 1) / view->cols * view->cols,
                blob_length(view->blob));

    if (view->input->cur < view->start)
        view->start = satsub(view->start,
                (view->start - view->input->cur + view->cols - 1) / view->cols * view->cols,
                0);

    assert(view->input->cur >= view->start);
    assert(view->input->cur < view_end(view));

    /* scrolling */
    if (view->start != old_start) {
        if (!(((ssize_t) view->start - (ssize_t) old_start) % (ssize_t) view->cols)) {
            view->scroll = ((ssize_t) view->start - (ssize_t) old_start) / (ssize_t) view->cols;
            if (view->scroll > (signed) view->rows || -view->scroll > (signed) view->rows) {
                view->scroll = 0;
                view_dirty_from(view, 0);
                return;
            }
            if (view->scroll > 0) {
                memmove(
                        view->dirty,
                        view->dirty + view->scroll,
                        (view->rows - view->scroll) * sizeof(*view->dirty)
                );
                for (size_t i = view->rows - view->scroll; i < view->rows; ++i)
                    view->dirty[i] = 1;
            }
            else {
                memmove(
                        view->dirty + (-view->scroll),
                        view->dirty,
                        (view->rows - (-view->scroll)) * sizeof(*view->dirty)
                );
                for (size_t i = 0; i < (size_t) -view->scroll; ++i)
                    view->dirty[i] = 1;
            }
        }
        else
            view_dirty_from(view, 0);
    }

}

