
#include "view.h"

#include <ctype.h>

#include "ansi.h"
#include "common.h"
#include "blob.h"
#include "term.h"
#include "input.h"

static void print(char const *s) { fputs(s, stdout); }

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
    view->color = !term.is_basic;
}

static unsigned view_max_cols(struct view const *view)
{
    assert(view->width);
    return (view->width - (view->pos_digits + strlen(": ") + strlen("||"))) / strlen("xx c");
}

static inline size_t satadd(size_t x, size_t y, size_t b) { assert(b >= 1); return min(b - 1, x + y); }
static inline size_t satsub(size_t x, size_t y, size_t a) { return x < y || x - y < a ? a : x - y; }

void view_set_cols(struct view *view, bool relative, int cols)
{
    unsigned max_cols = view_max_cols(view);
    if (relative) {
        if (cols >= 0)
            cols = satadd(view->cols,  cols, max_cols + 1);
        else
            cols = satsub(view->cols, -cols, 0);
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

    view_adjust(view);
}

void view_recompute(struct view *view, bool changed)
{
    unsigned old_rows = view->rows, old_cols = view->cols;
    unsigned digs = (bit_length(max(2, blob_length(view->blob)) - 1) + 3) / 4;

    if (digs > view->pos_digits) {
        view->pos_digits = digs;
        view_dirty_from(view, 0);
    }
    else if (!changed)
        return;

    view->rows = view->height;

    size_t max_cols = view_max_cols(view);
    if (!view->cols_fixed || view->cols > max_cols) {
        view->cols = max_cols;
        if (view->cols > CONFIG_ROUND_COLS)
            view->cols -= view->cols % CONFIG_ROUND_COLS;
        view->cols_fixed = false;
    }

    if (!view->rows)
        view->rows = 1;
    if (!view->cols)
        view->cols = 1;

    if (view->rows == old_rows && view->cols == old_cols)
        if (view->cols > 1) /* when too narrow for even one column, always redraw */
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
    char digits[0x10], *hexptr, *asciiptr;
    size_t hexlen, asciilen;
    FILE *hexfp, *asciifp;
    struct input *I = view->input;

    size_t const sel_start = min(I->cur, I->sel), sel_end = max(I->cur, I->sel);
    char const *last_color = NULL, *next_color;

    if (!(hexfp = open_memstream(&hexptr, &hexlen)))
        pdie("open_memstream");
    if (!(asciifp = open_memstream(&asciiptr, &asciilen)))
        pdie("open_memstream");
#define BOTH(EX) for (FILE *fp; ; ) { fp = hexfp; EX; fp = asciifp; EX; break; }

    if (off <= I->cur && I->cur < off + view->cols) {
        /* cursor in current line */
        if (view->color) fputs(color_yellow, hexfp);
        char const *space = &" "[I->cur >= ((size_t) 1 << 4 * view->pos_digits)]; /* in case cursor is just 1 past the end */
        fprintf(hexfp, "%0*zx%c%s", view->pos_digits, I->cur, I->mode_insert ? '+' : '>', space);
        if (view->color) fputs(color_normal, hexfp);
    }
    else {
        fprintf(hexfp, "%0*zx: ", view->pos_digits, off);
    }

    for (size_t j = 0, len = blob_length(view->blob); j < view->cols; ++j) {

        if (off + j < len) {
            sprintf(digits, "%02hhx", b = blob_at(view->blob, off + j));
        }
        else {
            b = ' ';
            strcpy(digits, "  ");
        }

        bool in_selection = I->mode == SELECT && off + j >= sel_start && off + j <= sel_end;

        if (off + j >= last) {
            for (size_t p = j; p < view->cols; ++p)
                fputs("   ", hexfp);
            break;
        }

        if (off + j == I->cur) {
            next_color = I->cur >= len ? color_red : color_yellow;
            BOTH(
                if (view->color && next_color != last_color) fputs(next_color, fp);
                fputs(inverse_video_on, fp);
            );

            if (!I->mode_ascii) {
                fputs(bold_on, hexfp);
                if (I->mode == INPUT && !I->low_nibble) fputs(underline_on, hexfp);
                fputc(digits[0], hexfp);
                if (I->mode == INPUT) fputs(I->low_nibble ? underline_on : underline_off, hexfp);
                fputc(digits[1], hexfp);
                if (I->mode == INPUT && I->low_nibble) fputs(underline_off, hexfp);
                fputs(bold_off, hexfp);
            }
            else
                fputs(digits, hexfp);

            if (I->mode == INPUT && I->mode_ascii)
                fprintf(asciifp, "%s%s%c%s%s", bold_on, underline_on, isprint(b) ? b : '.', underline_off, bold_off);
            else
                fputc(isprint(b) ? b : '.', asciifp);

            BOTH(
                fputs(inverse_video_off, fp);
            );
        }
        else {
            next_color = in_selection ? color_yellow
                       : isalnum(b) ? color_cyan
                       : isprint(b) ? color_blue
                       : !b ? color_red
                       : color_normal;
            BOTH(
                if (view->color && next_color != last_color)
                    fputs(next_color, fp);
            );
            fputc(isprint(b) ? b : '.', asciifp);
            fputs(digits, hexfp);
        }
        last_color = next_color;

        if (view->color)
            fputc(' ', hexfp);
        else {
            if (I->mode == SELECT && off + j + 1 == sel_start)
                fputc('<', hexfp);
            else if (I->mode == SELECT && off + j == sel_end)
                fputc('>', hexfp);
            else if (in_selection)
                fputc('_', hexfp);
            else
                fputc(' ', hexfp);
        }
    }
    if (view->color) fputs(color_normal, hexfp);

#undef BOTH
    if (fclose(hexfp))
        pdie("fclose");
    if (fclose(asciifp))
        pdie("fclose");
    printf("%s|%s%s|", hexptr, asciiptr, view->color ? color_normal : "");
    free(hexptr);
    free(asciiptr);

    fflush(stdout);  /* done immediately to prevent flushing partial lines later */
}

void view_update(struct view *view)
{
    if (view->input->mode == COMMAND || view->input->mode == SEARCH)
        /* cursor may still be visible by accident after a signal */
        fputs(hide_cursor, stdout);

    size_t last = max(blob_length(view->blob), view->input->cur + 1);

    if (view->scroll) {
        if (!term.is_basic)
            scroll(view->scroll);
        else
            view_dirty_from(view, 0);
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

