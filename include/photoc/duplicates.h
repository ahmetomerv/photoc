#ifndef PHOTOC_DUPLICATES_H
#define PHOTOC_DUPLICATES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "photoc/hash.h"

typedef struct {
    uint64_t file_size;
    unsigned char sha256[PHOTOC_SHA256_DIGEST_SIZE]; /* Shared by all paths. */
    char **paths;
    size_t count;
} photoc_duplicate_group;

typedef struct {
    photoc_duplicate_group *groups;
    size_t group_count;
    uint64_t files_visited;
    uint64_t files_hashed;
    uint64_t files_skipped;
    uint64_t errors;
    uint64_t duplicate_files; /* All files in groups of two or more. */
    uint64_t potential_savings; /* One retained copy per group. */
} photoc_duplicates_result;

/* A borrowed path and errno from a file-size or hashing failure. */
typedef void (*photoc_duplicates_warning_fn)(const char *path,
                                              int system_errno,
                                              void *user_data);

/* Examines regular files, regardless of extension. Full SHA-256 is used only
   for files that share both a size and a leading content prefix with another
   file (empty files use the known empty digest without reading). Groups and
   paths are sorted deterministically. Returns 0 when the walk completes,
   including when individual files were skipped (see errors); -1 with errno on
   a fatal error. On entry result need not be initialized, but clean up any
   previous result before reusing it. On return it owns groups and paths, and
   the caller must call photoc_duplicates_cleanup, including after a fatal
   error. The warning callback and user_data are borrowed and are not retained. */
int photoc_duplicates_find(const char *directory, bool recursive,
                           photoc_duplicates_warning_fn on_warning,
                           void *user_data, photoc_duplicates_result *result);

/* Same contract with bounded concurrency for full-hash candidates. Zero uses
   two workers, one is serial, and values above PHOTOC_MAX_WORKERS
   (thread_pool.h) are invalid. Small workloads and thread startup failures
   run serially. Warnings run on the caller, never on a worker. */
int photoc_duplicates_find_with_workers(const char *directory, bool recursive,
                           size_t workers,
                           photoc_duplicates_warning_fn on_warning,
                           void *user_data, photoc_duplicates_result *result);

void photoc_duplicates_cleanup(photoc_duplicates_result *result);

#endif
