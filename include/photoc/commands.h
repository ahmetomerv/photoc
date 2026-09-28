#ifndef PHOTOC_COMMANDS_H
#define PHOTOC_COMMANDS_H

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
int photoc_command_rename(const char *directory, const char *format,
                          bool recursive, bool apply);
int photoc_command_sort(const char *directory, bool recursive,
                        photoc_sort_mode mode, uint32_t gap_minutes,
                        bool apply);
int photoc_command_scrub(const char *path, bool recursive, bool in_place);

#endif
