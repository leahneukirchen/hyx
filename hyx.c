/*
 *
 * Copyright (c) 2016-2020 Lorenz Panny
 *
 * This is hyx version 2020.06.09.
 * Check for newer versions at https://yx7.cc/code.
 * Please report bugs to y@yx7.cc.
 *
 * This program is released under the MIT license; see license.txt.
 *
 */

#include "common.h"
#include "blob.h"
#include "view.h"
#include "input.h"
#include "ansi.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>


struct blob blob;
struct view view;
struct input input;

bool quit;

jmp_buf jmp_mainloop;


void die(char const *s)
{
    fprintf(stderr, "%s\n", s);
    view_text(&view);
    exit(EXIT_FAILURE);
}

void pdie(char const *s)
{
    perror(s);
    view_text(&view);
    exit(EXIT_FAILURE);
}

static void sighdlr(int num)
{
    switch (num) {
    case SIGWINCH:
        view.winch = true;
        break;
    case SIGTSTP:
	view.tstp = true;
	break;
    case SIGCONT:
	view.cont = true;
	break;
    case SIGALRM:
        /* This is used in parsing escape sequences,
         * but we don't need to do anything here. */
        break;
    case SIGINT:
        /* ignore */
        break;
    default:
        die("unrecognized signal");
    }
}

__attribute__((noreturn)) void version()
{
    printf("This is hyx version 2020.06.09.\n");
    exit(EXIT_SUCCESS);
}

__attribute__((noreturn)) void help(int st)
{
    bool tty = isatty(fileno(stdout));

    printf("\n");
    printf("    %shyx: a minimalistic hex editor%s\n",
            tty ? color_green : "", tty ? color_normal : "");
    printf("    ------------------------------\n\n");

    printf("    %sinvocation:%s hyx [filename]\n",
            tty ? color_yellow : "", tty ? color_normal : "");

    printf("    %sinvocation:%s [command] | hyx\n\n",
            tty ? color_yellow : "", tty ? color_normal : "");

    printf("    %skeys:%s\n\n",
            tty ? color_yellow : "", tty ? color_normal : "");
    printf("q               quit\n");
    printf("\n");
    printf("h, j, k, l      move cursor\n");
    printf("(hex digits)    edit bytes (in hex mode)\n");
    printf("(printable)     edit bytes (in ascii mode)\n");
    printf("i               switch between replace and insert modes\n");
    printf("tab             switch between hex and ascii input\n");
    printf("\n");
    printf("u               undo\n");
    printf("ctrl+r          redo\n");
    printf("\n");
    printf("v               start a selection\n");
    printf("escape          abort a selection\n");
    printf("x               delete current byte or selection\n");
    printf("s               substitute current byte or selection\n");
    printf("y               copy current byte or selection to clipboard\n");
    printf("p               paste\n");
    printf("P               paste and move cursor\n");
    printf("\n");
    printf("], [            increment/decrement number of columns\n");
    printf("\n");
    printf("ctrl+u, ctrl+d  scroll up/down one page\n");
    printf("g, G            jump to start/end of screen or file\n");
    printf("\n");
    printf(":               enter command (see below)\n");
    printf("\n");
    printf("/x (hex string) search for hexadecimal bytes\n");
    printf("/s (characters) search for ascii string\n");
    printf("n, N            jump to next/previous match\n");
    printf("\n");
    printf("ctrl+a, ctrl+x  increment/decrement current byte\n");
    printf("\n");

    printf("    %scommands:%s\n\n",
            tty ? color_yellow : "", tty ? color_normal : "");
    printf("$offset         jump to offset (supports hex/dec/oct)\n");
    printf("q               quit\n");
    printf("w [$filename]   save\n");
    printf("wq [$filename]  save and quit\n");
    printf("color y/n       toggle colors\n");

    printf("\n");

    exit(st);
}

int main(int argc, char **argv)
{
    struct sigaction sigact;

    char *filename = NULL;

    for (size_t i = 1; i < (size_t) argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
            help(0);
        else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version"))
            version();
        else if (!filename)
            filename = argv[i];
        else
            help(EXIT_FAILURE);
    }

    blob_init(&blob);
    if (!isatty(fileno(stdin))) {
        if (filename) help(EXIT_FAILURE);
        blob_load_stream(&blob, stdin);
        if (!freopen("/dev/tty", "r", stdin))
            pdie("could not reopen controlling TTY");
    }
    else {
        blob_load(&blob, filename);
    }

    view_init(&view, &blob, &input);
    input_init(&input, &view);

    /* set up signal handler */
    memset(&sigact, 0, sizeof(sigact));
    sigact.sa_handler = sighdlr;
    sigaction(SIGWINCH, &sigact, NULL);
    sigaction(SIGTSTP, &sigact, NULL);
    sigaction(SIGCONT, &sigact, NULL);
    sigaction(SIGALRM, &sigact, NULL);
    sigaction(SIGINT, &sigact, NULL);

    view_recompute(&view, true);
    view_visual(&view);

    do {
        /* This is used to redraw immediately when the window size changes. */
        setjmp(jmp_mainloop);

        if (view.winch) {
            view_recompute(&view, true);
            view.winch = false;
        }
        if (view.tstp) {
            view_text(&view);
            fflush(stdout);
            view.tstp = false;
            raise(SIGSTOP);
            /* likely continues with view.cont=true */
        }
        if (view.cont) {
            view_recompute(&view, true);
	    view_dirty_from(&view, 0);
	    view_visual(&view);
            view.cont = false;
        }
        assert(input.cur >= view.start && input.cur < view.start + view.rows * view.cols);
        view_update(&view);

        input_get(&input, &quit);

    } while (!quit);

    cursor_line(0);
    printf("%s", clear_screen);
    view_text(&view);

    input_free(&input);
    view_free(&view);
    blob_free(&blob);
}

