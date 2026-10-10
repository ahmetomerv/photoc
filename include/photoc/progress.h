#ifndef PHOTOC_PROGRESS_H
#define PHOTOC_PROGRESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    PHOTOC_PROGRESS_AUTO,
    PHOTOC_PROGRESS_NEVER
} photoc_progress_mode;

typedef uint64_t (*photoc_progress_clock_fn)(void *user_data);

typedef struct {
    FILE *stream;        /* Borrowed; stderr in the CLI. */
    const char *message; /* Borrowed until changed or finished. */
    photoc_progress_clock_fn clock;
    void *clock_data;
    size_t current;
    size_t total;
    uint64_t started_ms;
    uint64_t last_render_ms;
    uint64_t delay_ms; /* Show delay before the first frame. */
    size_t frame;
    bool enabled;
    bool started;
    bool visible;
    bool shown;
    bool has_total;
    bool interruption_reported;
} photoc_progress;

/* Only the owning (normally main) thread calls this API. No worker renders.
   The test initializer injects a stream, TTY decision and monotonic clock. */
void photoc_progress_init(photoc_progress *progress, photoc_progress_mode mode,
                          bool quiet, const char *message);
void photoc_progress_init_test(photoc_progress *progress, FILE *stream,
                               bool is_tty, photoc_progress_mode mode,
                               bool quiet, const char *message,
                               photoc_progress_clock_fn clock,
                               void *clock_data);
void photoc_progress_start(photoc_progress *progress);
void photoc_progress_set_message(photoc_progress *progress,
                                 const char *message);
void photoc_progress_set_total(photoc_progress *progress, size_t total);
void photoc_progress_update(photoc_progress *progress, size_t current);
void photoc_progress_increment(photoc_progress *progress);
void photoc_progress_clear(photoc_progress *progress);
/* A final line is printed only if progress appeared during this operation. */
void photoc_progress_finish(photoc_progress *progress, const char *message);
void photoc_progress_warn(photoc_progress *progress, const char *message);
void photoc_progress_fail(photoc_progress *progress, const char *message);
/* Clear before an ordinary diagnostic; later updates redraw naturally. */
void photoc_progress_before_diagnostic(photoc_progress *progress);
/* Pure formatters return the number of bytes that would have been written.
   Without a total, the running count is appended once it is nonzero. */
int photoc_progress_format(char *buffer, size_t size, const char *frame,
                           const char *message, size_t current, size_t total,
                           bool has_total);
int photoc_progress_format_elapsed(char *buffer, size_t size, uint64_t ms);
/* Installed only around CLI command execution. Handler records a flag; all
   output and cleanup happen on the caller thread. */
void photoc_progress_install_signals(void);
void photoc_progress_restore_signals(void);
bool photoc_progress_interrupted(void);

#endif
