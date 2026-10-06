#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE

#include "review_terminal.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t resized;
static volatile sig_atomic_t hung_up;

static void on_winch(int signal_number)
{
    (void)signal_number;
    resized = 1;
}

static void on_hup(int signal_number)
{
    (void)signal_number;
    hung_up = 1;
}

static int write_all(const char *bytes, size_t length)
{
    while (length != 0) {
        ssize_t written = write(STDOUT_FILENO, bytes, length);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) {
            if (written == 0)
                errno = EIO;
            return -1;
        }
        bytes += (size_t)written;
        length -= (size_t)written;
    }
    return 0;
}

int review_terminal_enter(review_terminal *terminal)
{
    if (terminal == NULL) {
        errno = EINVAL;
        return -1;
    }
    *terminal = (review_terminal){0};
    resized = 0;
    hung_up = 0;
    struct sigaction action = {0};
    sigemptyset(&action.sa_mask);
    action.sa_handler = on_winch;
    if (sigaction(SIGWINCH, &action, &terminal->previous_winch) != 0)
        return -1;
    terminal->have_winch = true;
    action.sa_handler = on_hup;
    if (sigaction(SIGHUP, &action, &terminal->previous_hup) != 0) {
        int saved_errno = errno;
        review_terminal_leave(terminal);
        errno = saved_errno;
        return -1;
    }
    terminal->have_hup = true;
    if (tcgetattr(STDIN_FILENO, &terminal->original) != 0) {
        int saved_errno = errno;
        review_terminal_leave(terminal);
        errno = saved_errno;
        return -1;
    }
    struct termios current = terminal->original;
    current.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    current.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    current.c_cc[VMIN] = 0;
    current.c_cc[VTIME] = 1;
    terminal->raw = true;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &current) != 0) {
        int saved_errno = errno;
        review_terminal_leave(terminal);
        errno = saved_errno;
        return -1;
    }
    static const char enter[] = "\033[?1049h\033[?25l";
    terminal->alternate = true;
    if (write_all(enter, sizeof(enter) - 1) != 0) {
        int saved_errno = errno;
        review_terminal_leave(terminal);
        errno = saved_errno;
        return -1;
    }
    return 0;
}

int review_terminal_leave(review_terminal *terminal)
{
    if (terminal == NULL)
        return 0;
    int failure = 0;
    int saved_errno = 0;
    if (terminal->raw && tcsetattr(STDIN_FILENO, TCSANOW,
                                   &terminal->original) != 0) {
        failure = -1;
        saved_errno = errno;
    }
    terminal->raw = false;
    if (terminal->alternate) {
        static const char leave[] = "\033[?25h\033[?1049l";
        if (write_all(leave, sizeof(leave) - 1) != 0 && failure == 0) {
            failure = -1;
            saved_errno = errno;
        }
    }
    terminal->alternate = false;
    if (terminal->have_hup)
        sigaction(SIGHUP, &terminal->previous_hup, NULL);
    if (terminal->have_winch)
        sigaction(SIGWINCH, &terminal->previous_winch, NULL);
    terminal->have_hup = false;
    terminal->have_winch = false;
    if (failure != 0)
        errno = saved_errno;
    return failure;
}

bool review_terminal_resized(void)
{
    bool value = resized != 0;
    resized = 0;
    return value;
}

bool review_terminal_hung_up(void)
{
    return hung_up != 0;
}

int review_terminal_read(review_terminal *terminal, review_key *key)
{
    if (terminal == NULL || key == NULL) {
        errno = EINVAL;
        return -1;
    }
    *key = REVIEW_KEY_NONE;
    unsigned char ch;
    ssize_t count = read(STDIN_FILENO, &ch, 1);
    if (count < 0 && errno == EINTR)
        return 0;
    if (count < 0)
        return -1;
    if (count == 0) {
        /* A lone Escape must not swallow a shortcut typed later. */
        terminal->escape_state = 0;
        return 0;
    }
    if (terminal->escape_state == 1) {
        terminal->escape_state = ch == '[' || ch == 'O' ? 2 : 0;
        return 0;
    }
    if (terminal->escape_state == 2) {
        terminal->escape_state = 0;
        if (ch == 'C')
            *key = REVIEW_KEY_NEXT;
        else if (ch == 'D')
            *key = REVIEW_KEY_PREVIOUS;
        return 0;
    }
    /* The visible keycaps use capitals; accept both letter cases. */
    if (ch >= 'A' && ch <= 'Z')
        ch = (unsigned char)(ch - 'A' + 'a');
    if (ch == 0x1b) {
        terminal->escape_state = 1;
    } else if (ch == 'l' || ch == ' ') {
        *key = REVIEW_KEY_NEXT;
    } else if (ch == 'h') {
        *key = REVIEW_KEY_PREVIOUS;
    } else if (ch == 'p') {
        *key = REVIEW_KEY_PICK;
    } else if (ch == 'x') {
        *key = REVIEW_KEY_REJECT;
    } else if (ch == 'u') {
        *key = REVIEW_KEY_UNMARK;
    } else if (ch == 'i') {
        *key = REVIEW_KEY_INFO;
    } else if (ch == '?') {
        *key = REVIEW_KEY_HELP;
    } else if (ch == 'q') {
        *key = REVIEW_KEY_QUIT;
    }
    return 0;
}

int review_terminal_print_safe(FILE *stream, const char *text)
{
    static const char digits[] = "0123456789ABCDEF";
    if (stream == NULL || text == NULL) {
        errno = EINVAL;
        return -1;
    }
    for (const unsigned char *p = (const unsigned char *)text; *p != 0; ++p) {
        if (*p >= 0x20 && *p <= 0x7e) {
            if (fputc(*p, stream) == EOF)
                return -1;
        } else if (fputc('\\', stream) == EOF ||
                   fputc('x', stream) == EOF ||
                   fputc(digits[*p >> 4], stream) == EOF ||
                   fputc(digits[*p & 15], stream) == EOF) {
            return -1;
        }
    }
    return 0;
}
