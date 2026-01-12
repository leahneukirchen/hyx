
#define _GNU_SOURCE

#include "input.h"

#include <ctype.h>
#include <errno.h>
#include <sys/time.h>

#include "ansi.h"
#include "common.h"
#include "blob.h"
#include "history.h"
#include "term.h"
#include "view.h"

extern jmp_buf jmp_mainloop; /* hyx.c */

void input_init(struct input *input, struct view *view)
{
    memset(input, 0, sizeof(*input));
    input->view = view;
}

void input_free(struct input *input)
{
    free(input->search.needle);
}

/*
 * The C standard forbids assigning arbitrary integers to an enum,
 * hence we do it the other way round: assign enumerated constants
 * to a general integer type.
 */
typedef uint16_t key;
enum {
    KEY_INTERRUPTED = 0x1000,
    KEY_SPECIAL_ESCAPE,
    KEY_SPECIAL_DELETE,
    KEY_SPECIAL_UP, KEY_SPECIAL_DOWN, KEY_SPECIAL_RIGHT, KEY_SPECIAL_LEFT,
    KEY_SPECIAL_PGUP, KEY_SPECIAL_PGDOWN,
    KEY_SPECIAL_HOME, KEY_SPECIAL_END,
};

static key getch(void)
{
    int c;
    errno = 0;
    if (EOF == (c = getc(stdin))) {
        if (errno == EINTR)
            return KEY_INTERRUPTED;
        pdie("getc");
    }
    return c;
}

static void ungetch(int c)
{
    if (c != ungetc(c, stdin))
        pdie("ungetc");
}

static key get_key(void)
{
    static const struct itimerval timeout = {{0}, {CONFIG_WAIT_ESCAPE / 1000000, CONFIG_WAIT_ESCAPE % 1000000}};
    static const struct itimerval stop = {0};

    /* FIXME Perhaps it'd be easier to just memorize everything after an escape
     * key until the timeout hits and parse it once we got the whole sequence? */
    static enum {
        none,
        discard,
        have_escape,
        have_bracket,
        need_tilde,
    } state = none;
    static key r;
    static uint64_t tick;

    key k;

    /* If we already saw an escape key and we're at the beginning of the
     * function again, it is likely we were interrupted by the timer.
     * Check if we've waited long enough for the rest of an escape sequence;
     * if yes, we either return the escape key or stop discarding input. */
    if ((state == have_escape || state == discard)
            && monotonic_microtime() - tick >= CONFIG_WAIT_ESCAPE) {
        switch (state) {
        case have_escape:
            state = none;
            r = KEY_SPECIAL_ESCAPE;
            goto stop_timer;
        case discard:
            state = none;
            if (setitimer(ITIMER_REAL, &stop, NULL))
                pdie("setitimer");
            break;
        default:
            die("unexpected state");
        }
    }

next:

    /* This might be a window size change or a timer interrupt, so we need to
     * go up to the main loop.  The state machine is untouched by this; we
     * can simply continue where we were as soon as we're called again. */
    if ((k = getch()) == KEY_INTERRUPTED)
        longjmp(jmp_mainloop, 0);

    switch (state) {

    case none:
        if (k != 0x1b)
            return k;

        state = have_escape;
start_timer:
        tick = monotonic_microtime();
        if (setitimer(ITIMER_REAL, &timeout, NULL))
            pdie("setitimer");
        goto next;

    case discard:
        goto next;

    case have_escape:
        if (k != '[') {
            ungetch(k);
            state = none;
            r = KEY_SPECIAL_ESCAPE;
            goto stop_timer;
        }
        state = have_bracket;
        goto next;

    case have_bracket:
        switch (k) {
        case 'A': state = none; r = KEY_SPECIAL_UP; goto stop_timer;
        case 'B': state = none; r = KEY_SPECIAL_DOWN; goto stop_timer;
        case 'C': state = none; r = KEY_SPECIAL_RIGHT; goto stop_timer;
        case 'D': state = none; r = KEY_SPECIAL_LEFT; goto stop_timer;
        case 'F': state = none; r = KEY_SPECIAL_END; goto stop_timer;
        case 'H': state = none; r = KEY_SPECIAL_HOME; goto stop_timer;
        case '3': state = need_tilde; r = KEY_SPECIAL_DELETE; goto next;
        case '5': state = need_tilde; r = KEY_SPECIAL_PGUP; goto next;
        case '6': state = need_tilde; r = KEY_SPECIAL_PGDOWN; goto next;
        case '7': state = need_tilde; r = KEY_SPECIAL_HOME; goto next;
        case '8': state = need_tilde; r = KEY_SPECIAL_END; goto next;
        default:
discard_sequence:
              /* We don't know this one. Enter discarding state and
               * wait for all the characters to come in. */
              state = discard;
              goto start_timer;
        }

    case need_tilde:
        if (k != '~')
            goto discard_sequence;
        state = none;
stop_timer:
        setitimer(ITIMER_REAL, &stop, NULL);
        return r;
    }

    __builtin_unreachable();
}

static char *get_line(char c)
{
    static size_t cap = 0, len = 0;
    static char *str = NULL;
    char *ret = NULL;

    /* FIXME this disrespects the view->color flag */
    printf("%s%s%c%s%s", bold_on, color_yellow, c, bold_off, color_normal);
    if (len) {
        str[len] = 0;
        fputs(str, stdout);
    }
    fputs(show_cursor, stdout);
    fflush(stdout);

    while (true) {
        if (len >= cap)
            str = realloc_strict(str, cap += 0x10);

        key k = get_key();

        switch (k) {
        case KEY_SPECIAL_ESCAPE:
            len = 0;
            goto out;
        case '\n':
            goto eol;
        case 0x7f: /* backspace */
            if (!len)
                goto out;
            --len;
            fputs("\b \b", stdout);
            fflush(stdout);
            break;
        default:
            if (!isprint(k))
                break;
            str[len++] = k;
            fputc(k, stdout);
            fflush(stdout);
            break;
        }
    }

eol:
    str[len] = 0;
    ret = str;
    cap = len = 0;
    str = NULL;

out:
    fputs(hide_cursor, stdout);
    fflush(stdout);

    return ret;
}

static void do_reset_soft(struct input *input)
{
    input->low_nibble = 0;
    input->cur_val = 0;
}

static void toggle_mode_select(struct input *input)
{
    struct view *V = input->view;
    struct blob *B = V->blob;

    switch (input->mode) {
    case INPUT:
        if (!blob_length(B))
            break;
        input->mode = SELECT;
        view_dirty_at(V, input->cur);  /* in case cursor is just 1 past the end */
        input->sel = (input->cur -= (input->cur >= blob_length(B)));
        view_dirty_at(V, input->cur);
        break;
    case SELECT:
        input->mode = INPUT;
        view_dirty_fromto(input->view, input->sel, input->cur + 1);
        view_dirty_fromto(input->view, input->cur, input->sel + 1);
        break;
    default:
        __builtin_unreachable();
    }
}

static size_t cur_bound(struct input const *input)
{
    size_t bound = blob_length(input->view->blob);
    bound += !bound || (input->mode == INPUT && input->mode_insert);
    assert(bound >= 1);
    return bound;
}

static size_t sat_sub_step(size_t x, size_t y, size_t z, size_t _)
{
    (void) _; assert(z);
    return x >= y * z ? x - y * z : x % z;
}

static size_t sat_add_step(size_t x, size_t y, size_t z, size_t b)
{
    assert(z); assert(x < b);
    return x + y * z < b ? x + y * z : b - 1 - (b - 1 - x) % z;
}

enum cur_move_direction { MOVE_LEFT, MOVE_RIGHT };
static void cur_move_rel(struct input *input, enum cur_move_direction dir, size_t off, size_t step)
{
    assert(input->cur <= cur_bound(input));

    struct view *V = input->view;

    do_reset_soft(input);
    view_dirty_at(V, input->cur);
    switch (dir) {
    case MOVE_LEFT:
        input->cur = sat_sub_step(input->cur, off, step, cur_bound(input));
        break;
    case MOVE_RIGHT:
        input->cur = sat_add_step(input->cur, off, step, cur_bound(input));
        break;
    default:
        __builtin_unreachable();
    }
    assert(input->cur < cur_bound(input));
    view_dirty_at(V, input->cur);
    view_adjust(V);
}

static void cur_adjust(struct input *input)
{
    struct view *V = input->view;

    do_reset_soft(input);
    if (input->cur >= cur_bound(input)) {
        view_dirty_at(V, input->cur);
        input->cur = cur_bound(input) - 1;
        view_dirty_at(V, input->cur);
        view_adjust(V);
    }
}

static void do_reset_hard(struct input *input)
{
    if (input->mode == SELECT)
        toggle_mode_select(input);
    input->mode_insert = input->mode_ascii = false;
    cur_adjust(input);
    view_dirty_at(input->view, input->cur);
}

static void toggle_mode_insert(struct input *input)
{
    struct view *V = input->view;

    if (!blob_can_move(V->blob)) {
        view_error(V, "can't insert: file is memory-mapped.");
        return;
    }

    if (input->mode != INPUT)
        return;
    input->mode_insert = !input->mode_insert;
    cur_adjust(input);
    view_dirty_at(V, input->cur);
}

static void toggle_mode_ascii(struct input *input)
{
    struct view *V = input->view;

    if (input->mode != INPUT)
        return;
    input->mode_ascii = !input->mode_ascii;
    view_dirty_at(V, input->cur);
}

static void do_yank(struct input *input)
{
    switch (input->mode) {
    case INPUT:
        input->sel = input->cur;
        input->mode = SELECT;
        /* fall-through */
    case SELECT:
        blob_yank(input->view->blob, min(input->sel, input->cur), absdiff(input->sel, input->cur) + 1);
        toggle_mode_select(input);
        break;
    default:
        __builtin_unreachable();
    }
}

static size_t do_paste(struct input *input)
{
    struct view *V = input->view;
    struct blob *B = V->blob;
    size_t retval;

    if (input->mode != INPUT)
        return 0;
    view_adjust(V);
    do_reset_soft(input);
    retval = blob_paste(B, input->cur, input->mode_insert ? INSERT : REPLACE);
    view_recompute(V, false);
    if (input->mode_insert)
        view_dirty_from(V, input->cur);
    else
        view_dirty_fromto(V, input->cur, input->cur + B->clipboard.len);

    return retval;
}

static bool do_delete(struct input *input, bool back)
{
    struct view *V = input->view;
    struct blob *B = V->blob;

    if (!blob_can_move(B)) {
        view_error(V, "can't delete: file is memory-mapped.");
        return false;
    }
    if (!blob_length(B))
        return false;

    if (back) {
        if (!input->cur)
            return false;
        cur_move_rel(input, MOVE_LEFT, 1, 1);
        if (!input->mode_insert)
            return true;
    }

    switch (input->mode) {
    case INPUT:
        if (input->cur >= blob_length(B)) {
            toggle_mode_insert(input);
            return false;
        }
        input->mode = SELECT;
        cur_adjust(input);
        input->sel = input->cur;
        /* fall-through */
    case SELECT:
        do_reset_soft(input);
        do_yank(input);
        if (input->cur > input->sel) {
            size_t tmp = input->cur;
            input->cur = input->sel;
            input->sel = tmp;
        }
        blob_delete(B, input->cur, input->sel - input->cur + 1, true);
        view_recompute(V, false);
        cur_adjust(input);
        view_dirty_from(V, input->cur);
        view_adjust(V);
        break;
    default:
        __builtin_unreachable();
    }
    return true;
}

static void do_quit(struct input *input, bool *quit, bool force)
{
    struct view *V = input->view;
    if (force || blob_is_saved(V->blob))
        *quit = true;
    else
        view_error(V, "unsaved changes! use :q! if you are sure.");
}

static void do_search_cont(struct input *input, ssize_t dir)
{
    struct view *V = input->view;
    size_t blen = blob_length(V->blob);

    if (!blen)
        return;

    size_t cur = dir > 0 ? min(input->cur, blen-1) : input->cur;
    ssize_t pos = blob_search(V->blob, input->search.needle, input->search.len, (cur + blen + dir) % blen, dir);

    if (pos < 0)
        return;

    view_dirty_at(V, input->cur);
    input->cur = pos;
    view_dirty_at(V, input->cur);
    view_adjust(V);
}

static void do_inc_dec(struct input *input, byte diff)
{
    struct view *V = input->view;
    struct blob *B = V->blob;

    /* should we do anything for selections? */
    if (input->mode != INPUT)
        return;

    if (input->cur >= blob_length(B))
        return;

    byte b = blob_at(B, input->cur) + diff;
    blob_replace(input->view->blob, input->cur, &b, 1, true);
    view_dirty_at(V, input->cur);
}

void do_home_end(struct input *input, size_t soft, size_t hard)
{
    assert(soft <= cur_bound(input));
    assert(hard <= cur_bound(input));

    struct view *V = input->view;

    do_reset_soft(input);
    view_dirty_at(V, input->cur);
    input->cur = input->cur == soft ? hard : soft;
    view_dirty_at(V, input->cur);
    if (input->mode == SELECT)
        view_dirty_from(V, 0); /* FIXME suboptimal */
    view_adjust(V);
}

void do_pgup_pgdown(struct input *input, size_t (*f)(size_t, size_t, size_t, size_t))
{
    struct view *V = input->view;

    size_t prev_start = V->start, prev_cur = input->cur;

    do_reset_soft(input);
    input->cur = f(input->cur, V->rows, V->cols, cur_bound(input));
    V->start = f(V->start, V->rows, V->cols, cur_bound(input));
    if (prev_start != V->start)
        view_dirty_from(V, 0);
    else if (prev_cur != input->cur) {
        view_dirty_at(V, prev_cur);
        view_dirty_at(V, input->cur);
    }
    view_adjust(V);
}


void input_cmd(struct input *input, char *str, bool *quit);
void input_search(struct input *input, char *str);

void input_get(struct input *input, bool *quit)
{
    struct view *V = input->view;
    struct blob *B = V->blob;

    if (input->mode == COMMAND || input->mode == SEARCH) {

        cursor_line(V->rows - 1); /* move to last line */
        fputs(clear_line, stdout);

        char c = input->mode == COMMAND ? ':' : '/';
        char *str = get_line(c);

        if (str) {
            switch (input->mode) {
            case COMMAND:
                input_cmd(input, str, quit);
                break;
            case SEARCH:
                input_search(input, str);
                break;
            default:
                __builtin_unreachable();
            }
            free(str);
        }

        input->mode = input->old_mode;
        view_dirty_from(V, 0);

        return; /* back to the main loop */
    }

    assert(input->mode == INPUT || input->mode == SELECT);

    key k = get_key();

    if (input->mode == INPUT) {

        if (input->mode_ascii && isprint(k)) {

            /* ascii input */

            if (!blob_length(B))
                input->mode_insert = true;

            byte b = k;
            if (input->mode_insert) {
                blob_insert(B, input->cur, &b, sizeof(b), true);
                view_recompute(V, false);
                view_dirty_from(V, input->cur);
            }
            else {
                blob_replace(B, input->cur, &b, sizeof(b), true);
                view_dirty_at(V, input->cur);
            }

            cur_move_rel(input, MOVE_RIGHT, 1, 1);
            return;
        }

        if ((k >= '0' && k <= '9') || (k >= 'a' && k <= 'f')) {

            /* hex input */

            if (!blob_length(B))
                input->mode_insert = true;

            if (input->mode_insert) {
                if (!input->low_nibble)
                    input->cur_val = 0;
                input->cur_val |= (k > '9' ? k - 'a' + 10 : k - '0') << 4 * (input->low_nibble = !input->low_nibble);
                if (input->low_nibble) {
                    blob_insert(B, input->cur, &input->cur_val, sizeof(input->cur_val), true);
                    view_recompute(V, false);
                    view_dirty_from(V, input->cur);
                }
                else {
                    blob_replace(B, input->cur, &input->cur_val, sizeof(input->cur_val), true);
                    view_dirty_at(V, input->cur);
                    cur_move_rel(input, MOVE_RIGHT, 1, 1);
                    return;
                }
            }
            else {
                input->cur_val = input->cur < blob_length(B) ? blob_at(B, input->cur) : 0;
                input->cur_val = input->cur_val & 0xf << 4 * input->low_nibble;
                input->cur_val |= (k > '9' ? k - 'a' + 10 : k - '0') << 4 * (input->low_nibble = !input->low_nibble);
                blob_replace(B, input->cur, &input->cur_val, sizeof(input->cur_val), true);
                view_dirty_at(V, input->cur);

                if (!input->low_nibble) {
                    cur_move_rel(input, MOVE_RIGHT, 1, 1);
                    return;
                }

            }

            view_adjust(V);
            return;
        }

    }

    /* function keys */

    switch (k) {

    case KEY_SPECIAL_ESCAPE:
        do_reset_hard(input);
        break;

    case 0x7f: /* backspace */
        do_delete(input, true);
        break;

    case 'x':
    case KEY_SPECIAL_DELETE:
        do_delete(input, false);
        break;

    case 'q':
        do_quit(input, quit, false);
        break;

    case 'v':
        toggle_mode_select(input);
        break;

    case 'y':
        do_yank(input);
        break;

    case 's':
        if (input->mode == SELECT && input->cur > input->sel) {
            size_t tmp = input->sel;
            input->sel = input->cur;
            input->cur = tmp;
        }
        if (do_delete(input, false) && !input->mode_insert)
            toggle_mode_insert(input);
        break;

    case 'p':
        do_paste(input);
        break;

    case 'P':
        cur_move_rel(input, MOVE_RIGHT, do_paste(input), 1);
        break;

    case 'i':
        toggle_mode_insert(input);
        break;

    case '\t':
        toggle_mode_ascii(input);
        break;

    case 'u':
        if (input->mode != INPUT) break;
        if (!blob_undo(B, &input->cur))
            break;
        view_recompute(V, false);
        cur_adjust(input);
        view_adjust(V);
        view_dirty_from(V, 0); /* FIXME suboptimal */
        break;

    case 0x12: /* ctrl + R */
        if (input->mode != INPUT) break;
        if (!blob_redo(B, &input->cur))
            break;
        view_recompute(V, false);
        cur_adjust(input);
        view_adjust(V);
        view_dirty_from(V, 0); /* FIXME suboptimal */
        break;

    case 0x7: /* ctrl + G */
        {
             char buf[256];
             snprintf(buf, sizeof(buf), "\"%s\" %s%s %zd/%zd bytes --%zd%%--",
                 input->view->blob->filename,
                 input->view->blob->alloc == BLOB_MMAP ? "[mmap]" : "",
                 input->view->blob->saved_dist ? "[modified]" : "[saved]",
                 input->cur,
                 blob_length(input->view->blob),
                 ((input->cur+1) * 100) / blob_length(input->view->blob));
             view_message(V, buf, NULL);
        }
        break;

    case 0xc: /* ctrl + L */
        view_dirty_from(V, 0);
        break;

    case ':':
        input->old_mode = input->mode;
        input->mode = COMMAND;
        break;

    case '/':
        input->old_mode = input->mode;
        input->mode = SEARCH;
        break;

    case 'n':
        do_search_cont(input, +1);
        break;

    case 'N':
        do_search_cont(input, -1);
        break;

    case 0x1: /* ctrl + A */
        do_inc_dec(input, 1);
        break;

    case 0x18: /* ctrl + X */
        do_inc_dec(input, -1);
        break;

    case 'j':
    case KEY_SPECIAL_DOWN:
        cur_move_rel(input, MOVE_RIGHT, 1, V->cols);
        break;

    case 'k':
    case KEY_SPECIAL_UP:
        cur_move_rel(input, MOVE_LEFT, 1, V->cols);
        break;

    case 'l':
    case KEY_SPECIAL_RIGHT:
        cur_move_rel(input, MOVE_RIGHT, 1, 1);
        break;

    case 'h':
    case KEY_SPECIAL_LEFT:
        cur_move_rel(input, MOVE_LEFT, 1, 1);
        break;

    case '^':
        cur_move_rel(input, MOVE_LEFT, (input->cur - V->start) % V->cols, 1);
        break;

    case '$':
        cur_move_rel(input, MOVE_RIGHT, V->cols-1 - (input->cur - V->start) % V->cols, 1);
        break;

    case 'g':
    case KEY_SPECIAL_HOME:
        do_home_end(input, min(V->start, cur_bound(input) - 1), 0);
        break;

    case 'G':
    case KEY_SPECIAL_END:
        do_home_end(input, min(V->start + V->rows * V->cols - 1, cur_bound(input) - 1), cur_bound(input) - 1);
        break;

    case 0x15: /* ctrl + U */
    case KEY_SPECIAL_PGUP:
        do_pgup_pgdown(input, sat_sub_step);
        break;

    case 0x4: /* ctrl + D */
    case KEY_SPECIAL_PGDOWN:
        do_pgup_pgdown(input, sat_add_step);
        break;

    case '[':
        view_set_cols(V, true, -1);
        break;

    case ']':
        view_set_cols(V, true, +1);
        break;

    }
}

void input_cmd(struct input *input, char *str, bool *quit)
{
    struct view *V = input->view;

    char *p;

    if (!(p = strtok(str, " ")))
        return;
    else if (!strcmp(p, "w") || !strcmp(p, "wq")) {
        switch (blob_save(V->blob, strtok(NULL, " "))) {
        case BLOB_SAVE_OK:
            if (!strcmp(p, "wq"))
                do_quit(input, quit, false);
            break;
        case BLOB_SAVE_FILENAME:
            view_error(V, "can't save: no filename.");
            break;
        case BLOB_SAVE_NONEXISTENT:
            view_error(V, "can't save: nonexistent path.");
            break;
        case BLOB_SAVE_PERMISSIONS:
            view_error(V, "can't save: insufficient permissions.");
            break;
        case BLOB_SAVE_BUSY:
            view_error(V, "can't save: file is busy.");
            break;
        default:
            die("can't save: unknown error");
        }
    }
    else if (!strcmp(p, "q") || !strcmp(p, "q!")) {
        do_quit(input, quit, !strcmp(p, "q!"));
    }
    else if (!strcmp(p, "colors") || !strcmp(p, "color") /* legacy */) {
        if ((p = strtok(NULL, " ")))
            V->color = *p == '1' || *p == 'y';
    }
    else if (!strcmp(p, "columns")) {
        if ((p = strtok(NULL, " "))) {
            if (!strcmp(p, "auto")) {
                view_set_cols(V, false, 0);
            }
            else {
                unsigned long long n = strtoull(p, &p, 0);
                if (!*p)
                    view_set_cols(V, false, n);
            }
        }
    }
    else if (!strcmp(p, "digits")) {
        if ((p = strtok(NULL, " "))) {
            if (!strcmp(p, "auto")) {
                V->pos_digits = 4; /* rather arbitrary */
            }
            else {
                unsigned long long n = strtoull(p, &p, 0);
                if (!*p)
                    V->pos_digits = n;
            }
            view_recompute(V, true);
        }
    }
    else {
        /* try to interpret the input as an offset */
        unsigned long long n = strtoull(p, &p, 0);
        if (!*p) {
            view_dirty_at(V, input->cur);
            if (n < cur_bound(input))
                input->cur = n;
            view_dirty_at(V, input->cur);
            view_adjust(V);
        }
    }
}

static unsigned unhex_digit(char c)
{
    assert(isxdigit(c));
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    die("not a hex digit");
}

static size_t unhex(byte **ret, char const *hex)
{
    size_t len = 0;
    *ret = malloc_strict(strlen(hex) / 2);
    for (char const *p = hex; *p; ) {
        while (isspace(*p)) ++p;
        if (!(isxdigit(p[0]) && isxdigit(p[1]))) {
            free(*ret);
            *ret = NULL;
            return 0;
        }
        (*ret)[len] = unhex_digit(*p++) << 4;
        (*ret)[len++] |= unhex_digit(*p++);
    }
    *ret = realloc_strict(*ret, len); /* shrink to what we actually used */
    return len;
}

/* NB: this accepts some technically invalid inputs */
static size_t utf8_to_ucs2(byte **ret, char const *str)
{
    size_t len = 0;
    *ret = malloc_strict(2 * strlen(str));
    for (uint32_t c, b; (c = *str++); ) {
        if (!(c & 0x80)) b = 0;
        else if ((c & 0xe0) == 0xc0) c &= 0x1f, b = 1;
        else if ((c & 0xf0) == 0xe0) c &= 0x0f, b = 2;
        else if ((c & 0xf8) == 0xf0) c &= 0x07, b = 3;
        else {
bad:
            free(*ret);
            *ret = NULL;
            return 0;
        }
        while (b--) {
            if ((*str & 0xc0) != 0x80) goto bad;
            c <<= 6, c |= (*str++ & 0x3f);
        }
        if (c >> 16) goto bad; /* not representable */
        (*ret)[len++] = c >> 0;
        (*ret)[len++] = c >> 8;
    }
    *ret = realloc_strict(*ret, len); /* shrink to what we actually used */
    return len;
}

void input_search(struct input *input, char *str)
{
    char *p, *q;

    input->search.len = 0;
    free(input->search.needle);
    input->search.needle = NULL;

    if (!(p = strtok(str, " ")))
        return;
    else if (!strcmp(p, "x") || !strcmp(p, "w")) {
        size_t (*fun)(byte **, char const *) = (*p == 'x') ? unhex : utf8_to_ucs2;
        if (!(q = strtok(NULL, " "))) {
            q = p;
            goto str;
        }
        input->search.len = fun(&input->search.needle, q);
    }
    else if (!strcmp(p, "s")) {
        if (!(q = strtok(NULL, "")))
            q = p;
str:
        input->search.len = strlen(q);
        input->search.needle = (byte *) strdup_strict(q);
    }
    else if (!(input->search.len = unhex(&input->search.needle, p))) {
        q = p;
        goto str;
    }

    do_search_cont(input, +1);
}

