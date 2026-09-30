#ifndef PHOTOC_PARSE_H
#define PHOTOC_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool help;
    bool version;
    bool verbose;
    bool quiet;
    bool no_progress;
    bool json;
    bool recursive;
    bool apply;
    bool gps;
    bool privacy;
    bool all_metadata;
    bool in_place;
    bool only_blurry;
    bool only_errors;
    bool print0;
    bool has_gps;
    bool no_gps;
    bool metadata;
    const char *command;
    const char *first_argument;
    const char *format;
    const char *sort_by;
    const char *gap;
    const char *quality;
    const char *target;
    const char *min_quality;
    const char *output_dir;
    const char *output_path;
    const char *columns;
    const char *thumb_size;
    const char *contact_sort;
    const char *threshold;
    const char *camera;
    const char *make;
    const char *iso;
    const char *aperture;
    const char *focal;
    const char *after;
    const char *before;
    size_t argument_count;
} photoc_cli_options;

typedef enum {
    PHOTOC_PARSE_OK,
    PHOTOC_PARSE_UNKNOWN_OPTION,
    PHOTOC_PARSE_MISSING_VALUE,
    PHOTOC_PARSE_DUPLICATE_OPTION
} photoc_parse_status;

/* command, first_argument, format, sort_by, gap, quality, target,
   min_quality, output_dir, threshold, query filter values, and error_arg
   borrow argv strings. */
photoc_parse_status photoc_parse_args(int argc, char *argv[],
                                      photoc_cli_options *options,
                                      const char **error_arg);

/* Accepts a decimal number followed by m or h (for example 30m or 2h).
   Zero is allowed. Returns false and leaves minutes unchanged on malformed
   input or when the converted value exceeds UINT32_MAX minutes. */
bool photoc_parse_gap_minutes(const char *text, uint32_t *minutes);

/* Positive integer bytes, optionally suffixed B, KB, MB, GB (decimal) or
   KiB, MiB, GiB (binary). Leaves *bytes unchanged on invalid/overflow. */
bool photoc_parse_size_bytes(const char *text, uint64_t *bytes);

/* Finite, nonnegative decimal value, optionally using scientific notation.
   Rejects whitespace, hexadecimal notation, NaN, infinity and range errors.
   Leaves *value unchanged on invalid input. */
bool photoc_parse_nonnegative_double(const char *text, double *value);

#endif
