#ifndef ANSI_H
#define ANSI_H

/* ANSI terminal escape sequences. */

static char const bold_on[] = "\x1b[1m", bold_off[] = "\x1b[22m"; /* not a typo */
static char const underline_on[] = "\x1b[4m", underline_off[] = "\x1b[24m";
static char const inverse_video_on[] = "\x1b[7m", inverse_video_off[] = "\x1b[27m";
static char const clear_screen[] = "\x1b[2J";
static char const clear_line[] = "\x1b[K";
static inline void cursor_line(unsigned n) { printf("\x1b[%uH", n + 1); }
static inline void cursor_column(unsigned n) { printf("\x1b[%uG", n + 1); }
static char const show_cursor[] = "\x1b[?25h", hide_cursor[] = "\x1b[?25l";
static char const color_black[] = "\x1b[30m";
static char const color_red[] = "\x1b[31m";
static char const color_green[] = "\x1b[32m";
static char const color_yellow[] = "\x1b[33m";
static char const color_blue[] = "\x1b[34m";
static char const color_purple[] = "\x1b[35m";
static char const color_cyan[] = "\x1b[36m";
static char const color_white[] = "\x1b[37m";
static char const color_normal[] = "\x1b[39m";

static char const enter_alternate_screen[] = "\x1b[?1049h\x1b[0;0H";
static char const leave_alternate_screen[] = "\x1b[?1049l";

#endif
