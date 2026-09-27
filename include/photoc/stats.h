#ifndef PHOTOC_STATS_H
#define PHOTOC_STATS_H

#include "photoc/photo.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *value;          /* Owned display value, e.g. "Model Z" or "2.8". */
    double numeric_value; /* Used only to order numeric ties. */
    uint64_t count;
} photoc_stats_count;

typedef struct {
    photoc_stats_count *items;
    size_t count;
    size_t capacity;
} photoc_stats_counts;

typedef struct {
    uint64_t total_photos;
    uint64_t total_file_size; /* Bytes from photos with known file size. */
    photoc_stats_counts camera_models;
    photoc_stats_counts iso_values;
    photoc_stats_counts apertures;
    photoc_stats_counts focal_lengths;
} photoc_stats_aggregate;

/* Initialize before use and clean up after use. All count-table values and
   arrays are owned by the aggregate; callers must not free them separately. */
void photoc_stats_init(photoc_stats_aggregate *aggregate);
void photoc_stats_cleanup(photoc_stats_aggregate *aggregate);

/* Adds a successfully loaded photo. Presence flags determine which metadata
   participates; unavailable fields are omitted. Returns 0, or -1 with errno
   set on invalid arguments, allocation failure, or counter overflow. On
   failure the aggregate may be partially updated but remains safe to clean. */
int photoc_stats_add_photo(photoc_stats_aggregate *aggregate,
                           const Photo *photo);

/* Sorts each table by descending count. Ties use bytewise model-name order
   for cameras and ascending numeric order for ISO, aperture, focal length. */
void photoc_stats_sort(photoc_stats_aggregate *aggregate);

#endif
