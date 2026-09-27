#include "photoc/stats.h"
#include "photoc/timestamp.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void photoc_stats_init(photoc_stats_aggregate *aggregate)
{
    if (aggregate != NULL) {
        *aggregate = (photoc_stats_aggregate){0};
    }
}

static void cleanup_counts(photoc_stats_counts *counts)
{
    for (size_t i = 0; i < counts->count; ++i) {
        free(counts->items[i].value);
    }
    free(counts->items);
    *counts = (photoc_stats_counts){0};
}

void photoc_stats_cleanup(photoc_stats_aggregate *aggregate)
{
    if (aggregate == NULL) {
        return;
    }
    cleanup_counts(&aggregate->camera_models);
    cleanup_counts(&aggregate->iso_values);
    cleanup_counts(&aggregate->apertures);
    cleanup_counts(&aggregate->focal_lengths);
    *aggregate = (photoc_stats_aggregate){0};
}

static int add_count(photoc_stats_counts *counts, const char *value,
                     double numeric_value)
{
    for (size_t i = 0; i < counts->count; ++i) {
        if (strcmp(counts->items[i].value, value) == 0) {
            if (counts->items[i].count == UINT64_MAX) {
                errno = EOVERFLOW;
                return -1;
            }
            ++counts->items[i].count;
            return 0;
        }
    }

    size_t length = strlen(value);
    if (length == SIZE_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    char *copy = malloc(length + 1);
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, value, length + 1);

    if (counts->count == counts->capacity) {
        size_t capacity = counts->capacity == 0 ? 4 : counts->capacity * 2;
        if (capacity < counts->capacity ||
            capacity > SIZE_MAX / sizeof(*counts->items)) {
            free(copy);
            errno = EOVERFLOW;
            return -1;
        }
        photoc_stats_count *items = realloc(counts->items,
                                            capacity * sizeof(*items));
        if (items == NULL) {
            free(copy);
            return -1;
        }
        counts->items = items;
        counts->capacity = capacity;
    }
    counts->items[counts->count++] = (photoc_stats_count){
        .value = copy,
        .numeric_value = numeric_value,
        .count = 1
    };
    return 0;
}

static int add_numeric(photoc_stats_counts *counts, double value)
{
    char label[64];
    int length = snprintf(label, sizeof(label), "%.15g", value);
    if (length < 0 || (size_t)length >= sizeof(label)) {
        errno = EOVERFLOW;
        return -1;
    }
    return add_count(counts, label, value);
}

int photoc_stats_add_photo(photoc_stats_aggregate *aggregate,
                           const Photo *photo)
{
    if (aggregate == NULL || photo == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (aggregate->total_photos == UINT64_MAX ||
        (photo->has_file_size &&
         photo->file_size > UINT64_MAX - aggregate->total_file_size)) {
        errno = EOVERFLOW;
        return -1;
    }

    if (photo->camera_model != NULL && photo->camera_model[0] != '\0' &&
        add_count(&aggregate->camera_models, photo->camera_model, 0.0) != 0) {
        return -1;
    }
    if (photo->has_iso && photo->iso != 0) {
        char label[32];
        int length = snprintf(label, sizeof(label), "%" PRIu32, photo->iso);
        if (length < 0 || (size_t)length >= sizeof(label)) {
            errno = EOVERFLOW;
            return -1;
        }
        if (add_count(&aggregate->iso_values, label, (double)photo->iso) != 0) {
            return -1;
        }
    }
    if (photo->has_aperture && isfinite(photo->aperture) &&
        photo->aperture > 0.0 &&
        add_numeric(&aggregate->apertures, photo->aperture) != 0) {
        return -1;
    }
    if (photo->has_focal_length && isfinite(photo->focal_length) &&
        photo->focal_length > 0.0 &&
        add_numeric(&aggregate->focal_lengths, photo->focal_length) != 0) {
        return -1;
    }

    ++aggregate->total_photos;
    if (photo->has_file_size) {
        aggregate->total_file_size += photo->file_size;
        ++aggregate->photos_with_file_size;
    }
    if (photoc_timestamp_is_valid(photo->capture_timestamp)) {
        if (!aggregate->has_capture_dates ||
            strcmp(photo->capture_timestamp, aggregate->earliest_capture) < 0) {
            memcpy(aggregate->earliest_capture, photo->capture_timestamp, 20);
        }
        if (!aggregate->has_capture_dates ||
            strcmp(photo->capture_timestamp, aggregate->latest_capture) > 0) {
            memcpy(aggregate->latest_capture, photo->capture_timestamp, 20);
        }
        aggregate->has_capture_dates = true;
    }
    return 0;
}

bool photoc_stats_average_file_size(const photoc_stats_aggregate *aggregate,
                                    double *output)
{
    if (aggregate == NULL || output == NULL ||
        aggregate->photos_with_file_size == 0) {
        return false;
    }
    *output = (double)aggregate->total_file_size /
              (double)aggregate->photos_with_file_size;
    return true;
}

static int compare_text_counts(const void *left, const void *right)
{
    const photoc_stats_count *a = left;
    const photoc_stats_count *b = right;
    if (a->count != b->count) {
        return a->count > b->count ? -1 : 1;
    }
    return strcmp(a->value, b->value);
}

static int compare_numeric_counts(const void *left, const void *right)
{
    const photoc_stats_count *a = left;
    const photoc_stats_count *b = right;
    if (a->count != b->count) {
        return a->count > b->count ? -1 : 1;
    }
    if (a->numeric_value != b->numeric_value) {
        return a->numeric_value < b->numeric_value ? -1 : 1;
    }
    return strcmp(a->value, b->value);
}

void photoc_stats_sort(photoc_stats_aggregate *aggregate)
{
    if (aggregate == NULL) {
        return;
    }
    if (aggregate->camera_models.count > 1) {
        qsort(aggregate->camera_models.items, aggregate->camera_models.count,
              sizeof(*aggregate->camera_models.items), compare_text_counts);
    }
    if (aggregate->iso_values.count > 1) {
        qsort(aggregate->iso_values.items, aggregate->iso_values.count,
              sizeof(*aggregate->iso_values.items), compare_numeric_counts);
    }
    if (aggregate->apertures.count > 1) {
        qsort(aggregate->apertures.items, aggregate->apertures.count,
              sizeof(*aggregate->apertures.items), compare_numeric_counts);
    }
    if (aggregate->focal_lengths.count > 1) {
        qsort(aggregate->focal_lengths.items, aggregate->focal_lengths.count,
              sizeof(*aggregate->focal_lengths.items), compare_numeric_counts);
    }
}
