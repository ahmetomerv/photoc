#ifndef PHOTOC_OUTPUT_H
#define PHOTOC_OUTPUT_H

#include "photoc/photo.h"

#include <stdbool.h>

typedef enum {
    PHOTOC_OUTPUT_NORMAL,
    PHOTOC_OUTPUT_QUIET,
    PHOTOC_OUTPUT_VERBOSE
} photoc_output_level;

/* Per-invocation configuration, passed by borrowed const pointer. No memory
   or streams are owned, and no state is retained. NULL means normal output.
   Result data is written by existing report/JSON writers regardless of level.
   Errors use error.h and are never filtered, including warnings that explain
   a nonzero exit. Only non-critical warnings use the filtered helper below. */
typedef struct {
    photoc_output_level level;
    bool json;
} photoc_output;

#if defined(__GNUC__) || defined(__clang__)
#define PHOTOC_OUTPUT_FORMAT(index, first)                                     \
    __attribute__((format(printf, index, first)))
#else
#define PHOTOC_OUTPUT_FORMAT(index, first)
#endif

/* Informational status/summary text: stdout in normal/verbose human mode,
   suppressed in quiet and JSON modes. These helpers preserve errno. */
void photoc_output_info(const photoc_output *output, const char *format, ...)
    PHOTOC_OUTPUT_FORMAT(2, 3);

/* Additional diagnostics: stderr only in verbose mode, with a command prefix.
   format includes any desired trailing newline. Strings are borrowed. */
void photoc_output_verbose(const photoc_output *output, const char *command,
                           const char *format, ...) PHOTOC_OUTPUT_FORMAT(3, 4);

/* A metadata warning that does not cause command failure. Suppressed in quiet
   mode; otherwise uses the existing error model on stderr, including JSON. */
void photoc_output_metadata_warning(const photoc_output *output,
                                    const char *command, const char *path,
                                    photoc_metadata_result result,
                                    int system_errno);

#undef PHOTOC_OUTPUT_FORMAT

#endif
