#ifndef PHOTOC_REVIEW_PREVIEW_H
#define PHOTOC_REVIEW_PREVIEW_H

#include "photoc/image.h"
#include "photoc/photo.h"

#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>

#define REVIEW_PREVIEW_SLOTS 3

typedef struct {
    size_t item_index;
    struct stat source;
    photoc_jpeg_buffer jpeg;
    unsigned long used_at;
    bool occupied;
} review_preview_entry;

typedef struct {
    review_preview_entry entries[REVIEW_PREVIEW_SLOTS];
    size_t bytes;
    unsigned long clock;
} review_preview_cache;

/* Identity check used by the session's metadata and preview caches. */
bool review_source_equal(const struct stat *left, const struct stat *right);

/* Returns a borrowed preview owned by cache, or NULL to stream the original.
   Previews are prepared lazily, kept only in memory, and never written to the
   source directory. Source identity is checked before reusing an entry. */
const photoc_jpeg_buffer *review_preview_get(review_preview_cache *cache,
                                              size_t item_index,
                                              const char *path,
                                              const Photo *metadata);

void review_preview_cleanup(review_preview_cache *cache);

#endif
