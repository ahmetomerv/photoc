#ifndef PHOTOC_SCAN_H
#define PHOTOC_SCAN_H

#include "photoc/photo.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t files_visited;    /* Regular files examined; directories excluded. */
    uint64_t jpeg_files_found; /* Regular .jpg/.jpeg files, even if unreadable. */
    uint64_t photos_parsed;    /* Successfully loaded JPEGs. */
    uint64_t skipped_files;    /* Non-JPEG files and JPEGs that could not load. */
    uint64_t errors;           /* JPEG load failures; a subset of skipped_files. */
} photoc_scan_stats;

/* Photo and its strings are borrowed and valid only during the callback.
   Return false to stop the scan after this photo. */
typedef bool (*photoc_scan_photo_fn)(const Photo *photo, void *user_data);

/* Called for each JPEG that could not be loaded. path is borrowed and valid
   only during the callback. system_errno is meaningful only for IO_ERROR and
   is zero otherwise. The callback may be NULL; errors are still counted. */
typedef void (*photoc_scan_warning_fn)(const char *path,
                                       photoc_metadata_result reason,
                                       int system_errno, void *user_data);

/* Scans regular files in directory. Symlinks are not followed. Returns 0 when
   complete, 1 when the photo callback stops the scan, or -1 with errno set on
   invalid arguments, directory traversal failure, or memory exhaustion.
   stats is zeroed before scanning and retains partial counts on early return.
   Per-file JPEG failures warn, increment errors/skipped_files, and do not stop
   the scan. Neither callback is retained; user_data is borrowed. */
int photoc_scan_directory(const char *directory, bool recursive,
                          photoc_scan_photo_fn on_photo,
                          photoc_scan_warning_fn on_warning,
                          void *user_data, photoc_scan_stats *stats);

#endif
