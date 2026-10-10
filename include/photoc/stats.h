#ifndef PHOTOC_STATS_H
#define PHOTOC_STATS_H

#include "photoc/photo.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *value;          /* Owned display value, e.g. "Model Z" or "2.8". */
    double numeric_value; /* Used only to order numeric ties. */
    uint64_t count;
    uint32_t width; /* Resolution buckets only; zero for other tables. */
    uint32_t height;
} photoc_stats_count;

typedef struct {
    photoc_stats_count *items;
    size_t count;
    size_t capacity;
    /* Private aggregation index; owned by the table and never read by callers.
       Invalid after photoc_stats_sort. */
    size_t *index;
    size_t index_capacity;
} photoc_stats_counts;

typedef struct {
    uint64_t *items; /* Owned scalar samples only, never pixel/image data. */
    size_t count;
    size_t capacity;
} photoc_stats_samples;

typedef struct {
    uint64_t total_photos;
    uint64_t total_file_size; /* Bytes from photos with known file size. */
    uint64_t photos_with_file_size;
    bool has_capture_dates;
    char earliest_capture[20]; /* Valid EXIF YYYY:MM:DD HH:MM:SS or empty. */
    char latest_capture[20];
    photoc_stats_counts camera_models;
    photoc_stats_counts iso_values;
    photoc_stats_counts apertures;
    photoc_stats_counts focal_lengths;
    photoc_stats_counts lens_models;
    photoc_stats_counts focal_lengths_35mm;
    photoc_stats_counts shutter_speeds; /* Seconds, not rounded camera stops. */
    photoc_stats_counts orientations; /* After applying orientation 5-8 swap. */
    photoc_stats_counts resolutions;  /* Exact stored width x height. */
    photoc_stats_counts years;
    photoc_stats_counts months; /* YYYY-MM, not pooled across years. */
    photoc_stats_counts days;   /* YYYY-MM-DD. */
    photoc_stats_counts hours;  /* Recorded local hour, 0-23. */
    photoc_stats_samples file_sizes;
    photoc_stats_samples capture_seconds;
    uint64_t session_count;
    uint64_t smallest_session;
    uint64_t largest_session;
} photoc_stats_aggregate;

/* Initialize before use and clean up after use. All count-table values and
   arrays are owned by the aggregate; callers must not free them separately. */
void photoc_stats_init(photoc_stats_aggregate *aggregate);
void photoc_stats_cleanup(photoc_stats_aggregate *aggregate);

/* Adds a successfully loaded photo. Presence flags determine which metadata
   participates; unavailable fields and invalid capture dates are omitted.
   Returns 0, or -1 with errno
   set on invalid arguments, allocation failure, or counter overflow. On
   failure the aggregate may be partially updated but remains safe to clean. */
int photoc_stats_add_photo(photoc_stats_aggregate *aggregate,
                           const Photo *photo);

/* Average bytes among photos whose file size is known. Returns false if no
   such photos exist or arguments are invalid; output is then unchanged. */
bool photoc_stats_average_file_size(const photoc_stats_aggregate *aggregate,
                                    double *output);

/* Available after photoc_stats_sort; odd middle or arithmetic mean of even
   middle sizes, without integer addition overflow. Unavailable is false. */
bool photoc_stats_median_file_size(const photoc_stats_aggregate *aggregate,
                                   double *output);

/* Sum of available rows. Percentages retain all parsed photos as denominator. */
uint64_t photoc_stats_available(const photoc_stats_counts *counts);
double photoc_stats_percentage(uint64_t count, uint64_t total);

/* Sorts each table by descending count. Ties use bytewise model-name order
   for strings and ascending numeric order for numeric values. Calendar ties
   sort lexically, resolutions by pixel count then label. Also sorts scalar
   samples and summarizes dated-photo sessions using the existing 60-minute
   gap rule. Invalid/missing dates are excluded, never inserted in the chain. */
void photoc_stats_sort(photoc_stats_aggregate *aggregate);

#endif
