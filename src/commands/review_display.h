#ifndef PHOTOC_REVIEW_DISPLAY_H
#define PHOTOC_REVIEW_DISPLAY_H

#include "photoc/image.h"
#include "photoc/photo.h"

#include <stdbool.h>
#include <stdio.h>

/* One slot belongs to one discovered item for the whole session. */
typedef struct {
    bool attempted;
    photoc_image_result result;
    double score;
} review_score_cache;

/* Computes through the shared sharpness API at most once per slot, including
   failures. path is borrowed. */
photoc_image_result review_score_get(review_score_cache *cache,
                                     const char *path);
void review_score_fail(review_score_cache *cache, photoc_image_result reason);

/* Format only available Photo fields. All strings are escaped for terminal
   output. photo and stream are borrowed; no ownership changes. */
void review_display_metadata(FILE *stream, const Photo *photo, bool details);
void review_display_sharpness(FILE *stream, const review_score_cache *cache);

#endif
