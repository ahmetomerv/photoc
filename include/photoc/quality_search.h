#ifndef PHOTOC_QUALITY_SEARCH_H
#define PHOTOC_QUALITY_SEARCH_H

#include <stdbool.h>
#include <stdint.h>

/* Probe returns the complete predicted output size for a JPEG quality.
   The callback owns any memory it allocates. Return -1 with errno on error. */
typedef int (*photoc_quality_probe_fn)(int quality, uint64_t *size,
                                       void *user_data);

typedef struct {
    int quality;
    uint64_t size;
    bool target_met;
} photoc_quality_choice;

/* Choose the highest quality in [minimum_quality, 100] that fits target.
   JPEG sizes are assumed to increase with quality. If even minimum_quality
   exceeds target, choose it and set target_met to false. Returns 0 on a
   completed search, or -1 with errno on invalid arguments/probe error. */
int photoc_quality_search(uint64_t target, int minimum_quality,
                          photoc_quality_probe_fn probe, void *user_data,
                          photoc_quality_choice *choice);

#endif
