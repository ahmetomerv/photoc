#define _POSIX_C_SOURCE 200809L

#include "photoc/progress.h"

#include <inttypes.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

enum { PROGRESS_DELAY_MS = 200, PROGRESS_RENDER_MS = 90 };
static const char *const frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼",
                                     "⠴", "⠦", "⠧", "⠇", "⠏"};
static const char clear_line[] = "\r\033[K";
static volatile sig_atomic_t interrupted_signal;
static struct sigaction previous_int;
static struct sigaction previous_term;
static bool handlers_installed;

static void interrupt_handler(int signal_number)
{
    int saved_errno = errno;
    interrupted_signal = signal_number;
    errno = saved_errno;
}

void photoc_progress_install_signals(void)
{
    struct sigaction action = {0};
    action.sa_handler = interrupt_handler;
    sigemptyset(&action.sa_mask);
    interrupted_signal = 0;
    if (sigaction(SIGINT, &action, &previous_int) != 0)
        return;
    if (sigaction(SIGTERM, &action, &previous_term) != 0) {
        sigaction(SIGINT, &previous_int, NULL);
        return;
    }
    handlers_installed = true;
}

void photoc_progress_restore_signals(void)
{
    if (handlers_installed) {
        sigaction(SIGINT, &previous_int, NULL);
        sigaction(SIGTERM, &previous_term, NULL);
        handlers_installed = false;
    }
    interrupted_signal = 0;
}

bool photoc_progress_interrupted(void)
{
    return interrupted_signal != 0;
}

static uint64_t monotonic_ms(void *user_data)
{
    (void)user_data;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

static void grouped(size_t value, char *buffer, size_t capacity)
{
    char reverse[sizeof(size_t) * 4];
    size_t n = 0;
    size_t digits = 0;
    do {
        if (digits != 0 && digits % 3 == 0)
            reverse[n++] = ',';
        reverse[n++] = (char)('0' + value % 10);
        value /= 10;
        ++digits;
    } while (value != 0);
    size_t i = 0;
    while (n != 0 && i + 1 < capacity)
        buffer[i++] = reverse[--n];
    buffer[i] = '\0';
}

int photoc_progress_format(char *buffer, size_t size, const char *frame,
                           const char *message, size_t current, size_t total,
                           bool has_total)
{
    if (!has_total) {
        if (current == 0)
            return snprintf(buffer, size, "%s %s", frame, message);
        char seen[sizeof(size_t) * 4];
        grouped(current, seen, sizeof(seen));
        return snprintf(buffer, size, "%s %s %s", frame, message, seen);
    }
    char count[sizeof(size_t) * 4];
    char sum[sizeof(size_t) * 4];
    grouped(current, count, sizeof(count));
    grouped(total, sum, sizeof(sum));
    size_t percentage = total == 0 ? 100
                        : current >= total
                            ? 100
                            : (size_t)((double)current * 100.0 / (double)total);
    return snprintf(buffer, size, "%s %s %s / %s (%zu%%)", frame, message,
                    count, sum, percentage);
}

int photoc_progress_format_elapsed(char *buffer, size_t size, uint64_t ms)
{
    if (ms < 1000)
        return snprintf(buffer, size, "%" PRIu64 "ms", ms);
    if (ms < 60000)
        return snprintf(buffer, size, "%.1fs", (double)ms / 1000.0);
    return snprintf(buffer, size, "%" PRIu64 "m %" PRIu64 "s", ms / 60000,
                    ms / 1000 % 60);
}

void photoc_progress_init_test(photoc_progress *progress, FILE *stream,
                               bool is_tty, photoc_progress_mode mode,
                               bool quiet, const char *message,
                               photoc_progress_clock_fn clock, void *clock_data)
{
    *progress = (photoc_progress){.stream = stream,
                                  .message = message,
                                  .clock = clock == NULL ? monotonic_ms : clock,
                                  .clock_data = clock_data,
                                  .enabled = mode == PHOTOC_PROGRESS_AUTO &&
                                             is_tty && !quiet};
}

void photoc_progress_init(photoc_progress *progress, photoc_progress_mode mode,
                          bool quiet, const char *message)
{
    photoc_progress_init_test(progress, stderr, isatty(STDERR_FILENO) != 0,
                              mode, quiet, message, NULL, NULL);
}

void photoc_progress_start(photoc_progress *progress)
{
    if (progress == NULL || !progress->enabled || progress->started)
        return;
    int saved_errno = errno;
    progress->started = true;
    progress->started_ms = progress->clock(progress->clock_data);
    errno = saved_errno;
}

static void render(photoc_progress *progress)
{
    if (progress == NULL || !progress->started || !progress->enabled)
        return;
    int saved_errno = errno;
    uint64_t now = progress->clock(progress->clock_data);
    if (now - progress->started_ms < PROGRESS_DELAY_MS ||
        (progress->visible &&
         now - progress->last_render_ms < PROGRESS_RENDER_MS)) {
        errno = saved_errno;
        return;
    }
    char line[512];
    photoc_progress_format(
        line, sizeof(line),
        frames[progress->frame % (sizeof(frames) / sizeof(frames[0]))],
        progress->message == NULL ? "Working..." : progress->message,
        progress->current, progress->total, progress->has_total);
    fputs(clear_line, progress->stream);
    fputs(line, progress->stream);
    fflush(progress->stream);
    progress->visible = true;
    progress->shown = true;
    progress->last_render_ms = now;
    ++progress->frame;
    errno = saved_errno;
}

void photoc_progress_set_message(photoc_progress *progress, const char *message)
{
    if (progress == NULL)
        return;
    progress->message = message;
    render(progress);
}

void photoc_progress_set_total(photoc_progress *progress, size_t total)
{
    if (progress == NULL)
        return;
    progress->has_total = true;
    progress->total = total;
    progress->current = 0;
    render(progress);
}

void photoc_progress_update(photoc_progress *progress, size_t current)
{
    if (progress == NULL)
        return;
    progress->current = current;
    render(progress);
}

void photoc_progress_increment(photoc_progress *progress)
{
    if (progress == NULL)
        return;
    if (progress->current != SIZE_MAX)
        ++progress->current;
    render(progress);
}

void photoc_progress_clear(photoc_progress *progress)
{
    if (progress == NULL)
        return;
    if (progress->visible) {
        int saved_errno = errno;
        fputs(clear_line, progress->stream);
        fflush(progress->stream);
        progress->visible = false;
        errno = saved_errno;
    }
}

void photoc_progress_before_diagnostic(photoc_progress *progress)
{
    photoc_progress_clear(progress);
}

static void complete(photoc_progress *progress, const char *marker,
                     const char *message, bool include_elapsed)
{
    if (progress == NULL)
        return;
    int saved_errno = errno;
    bool shown = progress->shown;
    photoc_progress_clear(progress);
    if (photoc_progress_interrupted()) {
        fprintf(progress->stream, "! Interrupted\n");
        fflush(progress->stream);
        progress->interruption_reported = true;
    } else if (shown) {
        if (include_elapsed) {
            char elapsed[32];
            uint64_t now = progress->clock(progress->clock_data);
            photoc_progress_format_elapsed(elapsed, sizeof(elapsed),
                                           now - progress->started_ms);
            fprintf(progress->stream, "%s %s in %s\n", marker, message,
                    elapsed);
        } else {
            fprintf(progress->stream, "%s %s\n", marker, message);
        }
        fflush(progress->stream);
    }
    progress->started = false;
    progress->shown = false;
    errno = saved_errno;
}

void photoc_progress_finish(photoc_progress *progress, const char *message)
{
    complete(progress, "✓", message, true);
}
void photoc_progress_warn(photoc_progress *progress, const char *message)
{
    complete(progress, "!", message, false);
}
void photoc_progress_fail(photoc_progress *progress, const char *message)
{
    complete(progress, "✗", message, false);
}
