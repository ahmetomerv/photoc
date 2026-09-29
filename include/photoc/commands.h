#ifndef PHOTOC_COMMANDS_H
#define PHOTOC_COMMANDS_H

#include "photoc/output.h"
#include "photoc/query.h"
#include "photoc/jpeg_write.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum { PHOTOC_SORT_BY_DATE, PHOTOC_SORT_BY_SESSION } photoc_sort_mode;

int photoc_command_unimplemented(const char *name);
typedef struct {
    int quality;
    uint64_t target_bytes; /* Zero selects fixed quality. */
    int min_quality;
    bool recursive;
    const char *output_dir; /* Borrowed; NULL writes beside each source. */
} photoc_compress_options;

int photoc_command_compress(const char *path,
                            const photoc_compress_options *options);
int photoc_command_exif(const char *path, bool json);
int photoc_command_duplicates(const char *directory, bool recursive, bool json);
int photoc_command_stats(const char *directory, bool recursive, bool json);
/* Read-only, date-first local EXIF timeline. Arguments are borrowed. */
int photoc_command_timeline(const char *directory, bool recursive,
                            uint32_t gap_minutes, bool json);
int photoc_command_timeline_with_output(const char *directory, bool recursive,
                                        uint32_t gap_minutes, bool json,
                                        const photoc_output *output);
int photoc_command_rename(const char *directory, const char *format,
                          bool recursive, bool apply);
int photoc_command_sort(const char *directory, bool recursive,
                        photoc_sort_mode mode, uint32_t gap_minutes,
                        bool apply);

#define PHOTOC_FOCUS_DEFAULT_THRESHOLD 100.0

/* Read-only JPEG analysis. Threshold is a finite, nonnegative review cutoff,
   not proof of blur. path is borrowed for the duration of this call. */
int photoc_command_focus(const char *path, bool recursive, double threshold,
                         bool only_blurry, bool json);
int photoc_command_scrub(const char *path, bool recursive, bool in_place);
int photoc_command_scrub_mode_with_output(const char *path, bool recursive,
                                          bool in_place, photoc_scrub_mode mode,
                                          const photoc_output *output);

/* Read-only JPEG readability audit. Paths/output are borrowed synchronously.
   Warnings do not fail the audit; only_errors excludes warning and OK rows. */
int photoc_command_check(const char *path, bool recursive, bool only_errors,
                         bool json);
int photoc_command_check_with_output(const char *path, bool recursive,
                                     bool only_errors, bool json,
                                     const photoc_output *output);

/* Read-only metadata search. All arguments are borrowed synchronously.
   json and print0 are exclusive. Scan/load failures return 1, no matches 0. */
int photoc_command_query(const char *path, const photoc_query *query,
                         bool recursive, bool json, bool print0);
int photoc_command_query_with_output(const char *path,
                                     const photoc_query *query, bool recursive,
                                     bool json, bool print0,
                                     const photoc_output *output);

/* Output-aware entry points. All arguments and output are borrowed only for
   the synchronous call. NULL output preserves the original normal behavior;
   the entry points above remain compatible wrappers. No context is retained. */
int photoc_command_compress_with_output(const char *path,
                                        const photoc_compress_options *options,
                                        const photoc_output *output);
int photoc_command_exif_with_output(const char *path, bool json,
                                    const photoc_output *output);
int photoc_command_duplicates_with_output(const char *directory, bool recursive,
                                          bool json,
                                          const photoc_output *output);
int photoc_command_stats_with_output(const char *directory, bool recursive,
                                     bool json, const photoc_output *output);
int photoc_command_rename_with_output(const char *directory, const char *format,
                                      bool recursive, bool apply,
                                      const photoc_output *output);
int photoc_command_sort_with_output(const char *directory, bool recursive,
                                    photoc_sort_mode mode, uint32_t gap_minutes,
                                    bool apply, const photoc_output *output);
int photoc_command_focus_with_output(const char *path, bool recursive,
                                     double threshold, bool only_blurry,
                                     bool json, const photoc_output *output);
int photoc_command_scrub_with_output(const char *path, bool recursive,
                                     bool in_place,
                                     const photoc_output *output);

#endif
