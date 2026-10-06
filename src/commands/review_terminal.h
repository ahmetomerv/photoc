#ifndef PHOTOC_REVIEW_TERMINAL_H
#define PHOTOC_REVIEW_TERMINAL_H

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <termios.h>

typedef enum {
    REVIEW_KEY_NONE,
    REVIEW_KEY_NEXT,
    REVIEW_KEY_PREVIOUS,
    REVIEW_KEY_PICK,
    REVIEW_KEY_REJECT,
    REVIEW_KEY_UNMARK,
    REVIEW_KEY_INFO,
    REVIEW_KEY_HELP,
    REVIEW_KEY_QUIT
} review_key;

typedef struct {
    struct termios original;
    struct sigaction previous_winch;
    struct sigaction previous_hup;
    bool have_winch;
    bool have_hup;
    bool raw;
    bool alternate;
    unsigned char escape_state;
} review_terminal;

/* stdin/stdout must already be TTYs. All setup is undone on failure. */
int review_terminal_enter(review_terminal *terminal);
int review_terminal_leave(review_terminal *terminal);
/* Reads at most one byte; VTIME wakes periodically for signals. */
int review_terminal_read(review_terminal *terminal, review_key *key);
bool review_terminal_resized(void);
bool review_terminal_hung_up(void);
/* Escape all control and non-ASCII bytes before writing untrusted text. */
int review_terminal_print_safe(FILE *stream, const char *text);

#endif
