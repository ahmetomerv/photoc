#ifndef PHOTOC_SCAN_H
#define PHOTOC_SCAN_H

#include "photoc/photo.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t files_visited; /* Regular files examined; directories excluded. */
    uint64_t
        jpeg_files_found;   /* Regular .jpg/.jpeg files, even if unreadable. */
    uint64_t photos_parsed; /* Successfully loaded JPEG/ARW photos. */
    uint64_t
        skipped_files; /* Unsupported files and photos that could not load. */
    uint64_t errors;   /* Metadata load failures; a subset of skipped_files. */
    uint64_t arw_files_found;      /* Regular .arw files, even if unreadable. */
    uint64_t metadata_files_found; /* JPEG + ARW candidates. */
} photoc_scan_stats;

/* Photo and its strings are borrowed and valid only during the callback.
   Return false to stop the scan after this photo. */
typedef bool (*photoc_scan_photo_fn)(const Photo *photo, void *user_data);

/* Called for each candidate photo that could not be loaded. path is borrowed and valid
   only during the callback. system_errno is meaningful only for IO_ERROR and
   is zero otherwise. The callback may be NULL; errors are still counted. */
typedef void (*photoc_scan_warning_fn)(const char *path,
                                       photoc_metadata_result reason,
                                       int system_errno, void *user_data);

/* Scans regular JPEG and Sony ARW files in directory. Symlinks are not followed. Returns 0 when
   complete, 1 when the photo callback stops the scan, or -1 with errno set on
   invalid arguments, directory traversal failure, or memory exhaustion.
   stats is zeroed before scanning and retains partial counts on early return.
   Per-file metadata failures warn, increment errors/skipped_files, and do not stop
   the scan. Loads use at most two workers and 32 buffered regular entries.
   Callbacks run serially on the caller in traversal order (the filesystem's
   order, not lexical order). Read-only loads may run ahead within a batch;
   stopping discards later results and excludes them from stats. Neither
   callback is retained; user_data is borrowed. */
int photoc_scan_directory(const char *directory, bool recursive,
                          photoc_scan_photo_fn on_photo,
                          photoc_scan_warning_fn on_warning, void *user_data,
                          photoc_scan_stats *stats);

/* Same contract, with an explicit bounded worker count: zero uses the default,
   one is serial, and values above PHOTOC_MAX_WORKERS (thread_pool.h) are
   invalid. Small batches run serially; thread startup failure falls back to
   serial loading. No new CLI option is introduced. */
int photoc_scan_directory_with_workers(const char *directory, bool recursive,
                                       size_t workers,
                                       photoc_scan_photo_fn on_photo,
                                       photoc_scan_warning_fn on_warning,
                                       void *user_data,
                                       photoc_scan_stats *stats);

/* Restrict discovery to an explicit format mask (format.h). Same scanner and
   bounded default workers. JPEG-only consumers must pass PHOTOC_FORMATS_JPEG. */
int photoc_scan_directory_filtered(const char *directory, bool recursive,
                                   unsigned int formats,
                                   photoc_scan_photo_fn on_photo,
                                   photoc_scan_warning_fn on_warning,
                                   void *user_data, photoc_scan_stats *stats);

#endif
