#include "photoc/stats.h"
#include "photoc/timestamp.h"
#include "photoc/session.h"

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
    cleanup_counts(&aggregate->lens_models);
    cleanup_counts(&aggregate->focal_lengths_35mm);
    cleanup_counts(&aggregate->shutter_speeds);
    cleanup_counts(&aggregate->orientations);
    cleanup_counts(&aggregate->resolutions);
    cleanup_counts(&aggregate->years);
    cleanup_counts(&aggregate->months);
    cleanup_counts(&aggregate->days);
    cleanup_counts(&aggregate->hours);
    free(aggregate->file_sizes.items);
    free(aggregate->capture_seconds.items);
    *aggregate = (photoc_stats_aggregate){0};
}

static int add_count(photoc_stats_counts *counts, const char *value,
                     double numeric_value, uint32_t width, uint32_t height)
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
        photoc_stats_count *items =
            realloc(counts->items, capacity * sizeof(*items));
        if (items == NULL) {
            free(copy);
            return -1;
        }
        counts->items = items;
        counts->capacity = capacity;
    }
    counts->items[counts->count++] =
        (photoc_stats_count){.value = copy,
                             .numeric_value = numeric_value,
                             .count = 1,
                             .width = width,
                             .height = height};
    return 0;
}

static int format_numeric_label(double value, char *label, size_t label_size)
{
    int length = snprintf(label, label_size, "%.15g", value);
    if (length < 0 || (size_t)length >= label_size) {
        errno = EOVERFLOW;
        return -1;
    }
    return 0;
}

/* Existing buckets stop at UINT64_MAX. Reject that before any bucket is
   updated so a later overflow cannot leave earlier counts incremented. */
static int reject_full_bucket(const photoc_stats_counts *counts,
                              const char *value)
{
    for (size_t i = 0; i < counts->count; ++i) {
        if (strcmp(counts->items[i].value, value) == 0 &&
            counts->items[i].count == UINT64_MAX) {
            errno = EOVERFLOW;
            return -1;
        }
    }
    return 0;
}

static int reserve_sample(photoc_stats_samples *samples)
{
    if (samples->count < samples->capacity)
        return 0;
    size_t capacity = samples->capacity == 0 ? 32 : samples->capacity * 2;
    if (capacity < samples->capacity ||
        capacity > SIZE_MAX / sizeof(*samples->items)) {
        errno = EOVERFLOW;
        return -1;
    }
    uint64_t *items = realloc(samples->items, capacity * sizeof(*items));
    if (items == NULL)
        return -1;
    samples->items = items;
    samples->capacity = capacity;
    return 0;
}

typedef struct {
    photoc_stats_counts *counts;
    const char *value; /* Borrowed until add_count copies it. */
    double numeric;
    uint32_t width;
    uint32_t height;
} stats_bucket;

static void queue_bucket(stats_bucket *buckets, size_t *count,
                         photoc_stats_counts *counts, const char *value,
                         double numeric)
{
    buckets[*count] =
        (stats_bucket){.counts = counts, .value = value, .numeric = numeric};
    ++*count;
}

static int queue_numeric(stats_bucket *buckets, size_t *count,
                         photoc_stats_counts *counts, char *label,
                         size_t label_size, double value)
{
    if (format_numeric_label(value, label, label_size) != 0)
        return -1;
    queue_bucket(buckets, count, counts, label, value);
    return 0;
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
         (aggregate->photos_with_file_size == UINT64_MAX ||
          photo->file_size > UINT64_MAX - aggregate->total_file_size))) {
        errno = EOVERFLOW;
        return -1;
    }

    stats_bucket buckets[13];
    char labels[13][64];
    size_t count = 0;
    if (photo->camera_model != NULL && photo->camera_model[0] != '\0')
        queue_bucket(buckets, &count, &aggregate->camera_models,
                     photo->camera_model, 0);
    if (photo->has_iso && photo->iso != 0)
        if (queue_numeric(buckets, &count, &aggregate->iso_values,
                          labels[count], sizeof(labels[count]),
                          photo->iso) != 0)
            return -1;
    if (photo->has_aperture && isfinite(photo->aperture) && photo->aperture > 0)
        if (queue_numeric(buckets, &count, &aggregate->apertures, labels[count],
                          sizeof(labels[count]), photo->aperture) != 0)
            return -1;
    if (photo->has_focal_length && isfinite(photo->focal_length) &&
        photo->focal_length > 0)
        if (queue_numeric(buckets, &count, &aggregate->focal_lengths,
                          labels[count], sizeof(labels[count]),
                          photo->focal_length) != 0)
            return -1;
    if (photo->lens_model != NULL && photo->lens_model[0] != '\0')
        queue_bucket(buckets, &count, &aggregate->lens_models,
                     photo->lens_model, 0);
    if (photo->has_focal_length_35mm && photo->focal_length_35mm != 0)
        if (queue_numeric(buckets, &count, &aggregate->focal_lengths_35mm,
                          labels[count], sizeof(labels[count]),
                          photo->focal_length_35mm) != 0)
            return -1;
    if (photo->has_exposure_time && isfinite(photo->exposure_time) &&
        photo->exposure_time > 0)
        if (queue_numeric(buckets, &count, &aggregate->shutter_speeds,
                          labels[count], sizeof(labels[count]),
                          photo->exposure_time) != 0)
            return -1;
    if (photo->has_width && photo->has_height && photo->width != 0 &&
        photo->height != 0) {
        uint32_t width = photo->width, height = photo->height;
        if (photo->has_orientation && photo->orientation >= 5 &&
            photo->orientation <= 8) {
            width = photo->height;
            height = photo->width;
        }
        queue_bucket(buckets, &count, &aggregate->orientations,
                     width > height   ? "landscape"
                     : width < height ? "portrait"
                                      : "square",
                     0);
        int length =
            snprintf(labels[count], sizeof(labels[count]),
                     "%" PRIu32 "x%" PRIu32, photo->width, photo->height);
        if (length < 0 || (size_t)length >= sizeof(labels[count])) {
            errno = EOVERFLOW;
            return -1;
        }
        queue_bucket(buckets, &count, &aggregate->resolutions, labels[count],
                     (double)((uint64_t)photo->width * photo->height));
        buckets[count - 1].width = photo->width;
        buckets[count - 1].height = photo->height;
    }
    uint64_t captured;
    bool valid_capture =
        photoc_timestamp_to_seconds(photo->capture_timestamp, &captured);
    if (valid_capture) {
        char *year = labels[count];
        memcpy(year, photo->capture_timestamp, 4);
        year[4] = '\0';
        queue_bucket(buckets, &count, &aggregate->years, year, 0);
        char *month = labels[count];
        memcpy(month, photo->capture_timestamp, 7);
        month[4] = '-';
        month[7] = '\0';
        queue_bucket(buckets, &count, &aggregate->months, month, 0);
        char *day = labels[count];
        memcpy(day, photo->capture_timestamp, 10);
        day[4] = '-';
        day[7] = '-';
        day[10] = '\0';
        queue_bucket(buckets, &count, &aggregate->days, day, 0);
        unsigned int hour =
            (unsigned int)(photo->capture_timestamp[11] - '0') * 10u +
            (unsigned int)(photo->capture_timestamp[12] - '0');
        if (queue_numeric(buckets, &count, &aggregate->hours, labels[count],
                          sizeof(labels[count]), hour) != 0)
            return -1;
    }
    /* Reject count overflows before incrementing any table. Allocation errors
       may leave new rows but the caller aborts and cleans up the aggregate. */
    for (size_t i = 0; i < count; ++i)
        if (reject_full_bucket(buckets[i].counts, buckets[i].value) != 0)
            return -1;
    if ((photo->has_file_size && reserve_sample(&aggregate->file_sizes) != 0) ||
        (valid_capture && reserve_sample(&aggregate->capture_seconds) != 0))
        return -1;
    for (size_t i = 0; i < count; ++i)
        if (add_count(buckets[i].counts, buckets[i].value, buckets[i].numeric,
                      buckets[i].width, buckets[i].height) != 0)
            return -1;

    ++aggregate->total_photos;
    if (photo->has_file_size) {
        aggregate->total_file_size += photo->file_size;
        ++aggregate->photos_with_file_size;
        aggregate->file_sizes.items[aggregate->file_sizes.count++] =
            photo->file_size;
    }
    if (valid_capture) {
        aggregate->capture_seconds.items[aggregate->capture_seconds.count++] =
            captured;
        if (!aggregate->has_capture_dates ||
            strcmp(photo->capture_timestamp, aggregate->earliest_capture) < 0)
            memcpy(aggregate->earliest_capture, photo->capture_timestamp, 20);
        if (!aggregate->has_capture_dates ||
            strcmp(photo->capture_timestamp, aggregate->latest_capture) > 0)
            memcpy(aggregate->latest_capture, photo->capture_timestamp, 20);
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

static int compare_samples(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *)left, b = *(const uint64_t *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

static void summarize_sessions(photoc_stats_aggregate *aggregate)
{
    aggregate->session_count = 0;
    aggregate->smallest_session = 0;
    aggregate->largest_session = 0;
    uint64_t in_session = 0;
    const photoc_stats_samples *dates = &aggregate->capture_seconds;
    for (size_t i = 0; i <= dates->count; ++i) {
        if (i == dates->count ||
            (i != 0 &&
             photoc_session_starts_new(dates->items[i - 1], dates->items[i],
                                       PHOTOC_SESSION_DEFAULT_GAP_MINUTES))) {
            if (in_session != 0) {
                ++aggregate->session_count;
                if (aggregate->smallest_session == 0 ||
                    in_session < aggregate->smallest_session)
                    aggregate->smallest_session = in_session;
                if (in_session > aggregate->largest_session)
                    aggregate->largest_session = in_session;
                in_session = 0;
            }
        }
        if (i < dates->count)
            ++in_session;
    }
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
    photoc_stats_counts *text[] = {&aggregate->lens_models,
                                   &aggregate->orientations, &aggregate->years,
                                   &aggregate->months, &aggregate->days};
    photoc_stats_counts *numeric[] = {
        &aggregate->focal_lengths_35mm, &aggregate->shutter_speeds,
        &aggregate->resolutions, &aggregate->hours};
    for (size_t i = 0; i < sizeof(text) / sizeof(text[0]); ++i)
        if (text[i]->count > 1)
            qsort(text[i]->items, text[i]->count, sizeof(*text[i]->items),
                  compare_text_counts);
    for (size_t i = 0; i < sizeof(numeric) / sizeof(numeric[0]); ++i)
        if (numeric[i]->count > 1)
            qsort(numeric[i]->items, numeric[i]->count,
                  sizeof(*numeric[i]->items), compare_numeric_counts);
    photoc_stats_samples *samples[] = {&aggregate->file_sizes,
                                       &aggregate->capture_seconds};
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i)
        if (samples[i]->count > 1)
            qsort(samples[i]->items, samples[i]->count,
                  sizeof(*samples[i]->items), compare_samples);
    summarize_sessions(aggregate);
}

uint64_t photoc_stats_available(const photoc_stats_counts *counts)
{
    uint64_t available = 0;
    for (size_t i = 0; i < counts->count; ++i)
        available += counts->items[i].count;
    return available;
}

double photoc_stats_percentage(uint64_t count, uint64_t total)
{
    return total == 0 ? 0 : 100.0 * (double)count / (double)total;
}

bool photoc_stats_median_file_size(const photoc_stats_aggregate *aggregate,
                                   double *output)
{
    if (aggregate == NULL || output == NULL || aggregate->file_sizes.count == 0)
        return false;
    size_t middle = aggregate->file_sizes.count / 2;
    uint64_t upper = aggregate->file_sizes.items[middle];
    if (aggregate->file_sizes.count % 2 != 0)
        *output = (double)upper;
    else {
        uint64_t lower = aggregate->file_sizes.items[middle - 1];
        *output = (double)lower + (double)(upper - lower) / 2.0;
    }
    return true;
}
