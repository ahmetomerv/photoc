#include "photoc/stats.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: %s (errno=%d)\n", __FILE__, __LINE__,     \
                    #condition, errno);                                        \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

static char *copy_text(const char *value)
{
    size_t length = strlen(value);
    char *copy = malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, value, length + 1);
    }
    return copy;
}

static void add_photo(photoc_stats_aggregate *aggregate, const char *model,
                      uint64_t size, bool has_size, uint32_t iso,
                      double aperture, double focal_length,
                      const char *captured)
{
    Photo photo = {0};
    if (photo_init(&photo, "test.jpg") != 0) {
        CHECK(false);
        return;
    }
    if (model != NULL) {
        photo.camera_model = copy_text(model);
        if (photo.camera_model == NULL) {
            CHECK(false);
            photo_cleanup(&photo);
            return;
        }
    }
    if (captured != NULL) {
        photo.capture_timestamp = copy_text(captured);
        if (photo.capture_timestamp == NULL) {
            CHECK(false);
            photo_cleanup(&photo);
            return;
        }
    }
    photo.has_file_size = has_size;
    photo.file_size = size;
    photo.has_iso = iso != 0;
    photo.iso = iso;
    photo.has_aperture = aperture > 0.0;
    photo.aperture = aperture;
    photo.has_focal_length = focal_length > 0.0;
    photo.focal_length = focal_length;

    CHECK(photoc_stats_add_photo(aggregate, &photo) == 0);
    photo_cleanup(&photo);
}

static void check_count(const photoc_stats_counts *counts, size_t index,
                        const char *value, uint64_t count)
{
    CHECK(index < counts->count);
    if (index < counts->count) {
        CHECK(strcmp(counts->items[index].value, value) == 0);
        CHECK(counts->items[index].count == count);
    }
}

static void test_aggregation(void)
{
    photoc_stats_aggregate aggregate;
    photoc_stats_init(&aggregate);

    add_photo(&aggregate, "Beta", 100, true, 200, 2.8, 50.0,
              "2025:01:01 00:00:00");
    add_photo(&aggregate, "Alpha", 200, true, 100, 4.0, 35.0,
              "2024:12:31 23:59:59");
    add_photo(&aggregate, "Beta", 300, true, 200, 2.8, 35.0,
              "invalid date");
    add_photo(&aggregate, "Alpha", 400, true, 100, 4.0, 50.0,
              "2026:05:10 12:30:00");
    add_photo(&aggregate, "Gamma", 500, true, 400, 2.8, 85.0, NULL);
    add_photo(&aggregate, "Gamma", 600, true, 800, 5.6, 85.0,
              "2020:02:29 08:00:00");
    add_photo(&aggregate, "Gamma", 700, true, 800, 5.6, 85.0,
              "2019:02:29 08:00:00");
    add_photo(&aggregate, NULL, 999, false, 0, 0.0, 0.0, NULL);

    CHECK(aggregate.total_photos == 8);
    CHECK(aggregate.total_file_size == 2800);
    CHECK(aggregate.photos_with_file_size == 7);
    double average = 0.0;
    CHECK(photoc_stats_average_file_size(&aggregate, &average));
    CHECK(average == 400.0);
    CHECK(aggregate.has_capture_dates);
    CHECK(strcmp(aggregate.earliest_capture, "2020:02:29 08:00:00") == 0);
    CHECK(strcmp(aggregate.latest_capture, "2026:05:10 12:30:00") == 0);
    CHECK(aggregate.camera_models.count == 3);
    CHECK(aggregate.iso_values.count == 4);
    CHECK(aggregate.apertures.count == 3);
    CHECK(aggregate.focal_lengths.count == 3);

    photoc_stats_sort(&aggregate);
    check_count(&aggregate.camera_models, 0, "Gamma", 3);
    check_count(&aggregate.camera_models, 1, "Alpha", 2);
    check_count(&aggregate.camera_models, 2, "Beta", 2);
    check_count(&aggregate.iso_values, 0, "100", 2);
    check_count(&aggregate.iso_values, 1, "200", 2);
    check_count(&aggregate.iso_values, 2, "800", 2);
    check_count(&aggregate.iso_values, 3, "400", 1);
    check_count(&aggregate.apertures, 0, "2.8", 3);
    check_count(&aggregate.apertures, 1, "4", 2);
    check_count(&aggregate.apertures, 2, "5.6", 2);
    check_count(&aggregate.focal_lengths, 0, "85", 3);
    check_count(&aggregate.focal_lengths, 1, "35", 2);
    check_count(&aggregate.focal_lengths, 2, "50", 2);

    photoc_stats_cleanup(&aggregate);
    CHECK(aggregate.total_photos == 0 && aggregate.total_file_size == 0);
    CHECK(!aggregate.has_capture_dates && aggregate.photos_with_file_size == 0);
    CHECK(aggregate.camera_models.items == NULL);
    photoc_stats_cleanup(&aggregate);
}

static void test_one_photo_and_missing_exif(void)
{
    photoc_stats_aggregate aggregate;
    photoc_stats_init(&aggregate);
    double average = -1.0;
    CHECK(!photoc_stats_average_file_size(&aggregate, &average));
    CHECK(average == -1.0);

    add_photo(&aggregate, "Solo", 1234, true, 640, 1.8, 85.0,
              "2023:04:05 06:07:08");
    photoc_stats_sort(&aggregate);
    CHECK(aggregate.total_photos == 1 && aggregate.total_file_size == 1234);
    CHECK(photoc_stats_average_file_size(&aggregate, &average));
    CHECK(average == 1234.0);
    CHECK(strcmp(aggregate.earliest_capture, "2023:04:05 06:07:08") == 0);
    CHECK(strcmp(aggregate.latest_capture, "2023:04:05 06:07:08") == 0);
    check_count(&aggregate.camera_models, 0, "Solo", 1);
    check_count(&aggregate.iso_values, 0, "640", 1);
    check_count(&aggregate.apertures, 0, "1.8", 1);
    check_count(&aggregate.focal_lengths, 0, "85", 1);
    photoc_stats_cleanup(&aggregate);

    add_photo(&aggregate, NULL, 512, true, 0, 0.0, 0.0, NULL);
    CHECK(aggregate.total_photos == 1 && aggregate.total_file_size == 512);
    CHECK(photoc_stats_average_file_size(&aggregate, &average));
    CHECK(average == 512.0);
    CHECK(!aggregate.has_capture_dates);
    CHECK(aggregate.camera_models.count == 0 && aggregate.iso_values.count == 0);
    CHECK(aggregate.apertures.count == 0 && aggregate.focal_lengths.count == 0);
    photoc_stats_cleanup(&aggregate);
}

static void test_errors(void)
{
    photoc_stats_aggregate aggregate;
    photoc_stats_init(&aggregate);
    Photo photo = {0};
    photo.has_file_size = true;
    photo.file_size = 2;

    errno = 0;
    CHECK(photoc_stats_add_photo(NULL, &photo) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(photoc_stats_add_photo(&aggregate, NULL) == -1 && errno == EINVAL);
    aggregate.total_file_size = UINT64_MAX - 1;
    errno = 0;
    CHECK(photoc_stats_add_photo(&aggregate, &photo) == -1 && errno == EOVERFLOW);
    CHECK(aggregate.total_photos == 0);

    photoc_stats_cleanup(&aggregate);
    photoc_stats_sort(NULL);
    photoc_stats_cleanup(NULL);
}

int main(void)
{
    test_aggregation();
    test_one_photo_and_missing_exif();
    test_errors();
    if (failures != 0) {
        fprintf(stderr, "%d aggregation test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
