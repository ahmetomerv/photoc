#include "photoc/timeline.h"
#include "photoc/session.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s (errno=%d)\n", __FILE__, __LINE__,      \
                    #condition, errno);                                        \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static char *copy_text(const char *text)
{
    size_t size = strlen(text) + 1;
    char *value = malloc(size);
    if (value != NULL)
        memcpy(value, text, size);
    return value;
}

static void add_photo(photoc_timeline *timeline, const char *captured,
                      const char *camera, uint64_t bytes, double focal,
                      double aperture, uint32_t iso)
{
    Photo photo = {0};
    if (captured != NULL) {
        photo.capture_timestamp = copy_text(captured);
        CHECK(photo.capture_timestamp != NULL);
    }
    if (camera != NULL) {
        photo.camera_model = copy_text(camera);
        CHECK(photo.camera_model != NULL);
    }
    photo.file_size = bytes;
    photo.has_file_size = true;
    photo.focal_length = focal;
    photo.has_focal_length = focal > 0;
    photo.aperture = aperture;
    photo.has_aperture = aperture > 0;
    photo.iso = iso;
    photo.has_iso = iso > 0;
    CHECK(photoc_timeline_add(timeline, &photo) == 0);
    photo_cleanup(
        &photo); /* All report data must survive the borrowed Photo. */
}

static void test_grouping(void)
{
    photoc_timeline timeline;
    photoc_timeline_init(&timeline);
    CHECK(photoc_timeline_finish(&timeline, 60) == 0);
    CHECK(timeline.session_count == 0 && timeline.date_count == 0);
    add_photo(&timeline, "2026:08:24 00:00:00", "Camera B", 100, 35, 4, 800);
    add_photo(&timeline, "2026:08:23 09:30:01", NULL, 200, 0, 0, 0);
    add_photo(&timeline, "2026:08:23 23:59:59", "Camera C", 300, 85, 8, 1600);
    add_photo(&timeline, "2026:08:23 08:00:00", "Camera B", 400, 24, 5.6, 100);
    add_photo(&timeline, "2026:08:23 08:30:00", "Camera A", 500, 9, 4, 400);
    add_photo(&timeline, NULL, NULL, 1000, 0, 0, 0);
    add_photo(&timeline, "2026:02:30 10:00:00", NULL, 1000, 0, 0, 0);
    CHECK(photoc_timeline_finish(&timeline, 60) == 0);
    CHECK(timeline.photo_count == 5 && timeline.session_count == 4 &&
          timeline.date_count == 2);
    CHECK(timeline.skipped_missing_timestamp == 1 &&
          timeline.skipped_invalid_timestamp == 1);
    const photoc_timeline_session *first = &timeline.sessions[0];
    CHECK(strcmp(first->date, "2026-08-23") == 0);
    CHECK(strcmp(first->start, "2026:08:23 08:00:00") == 0);
    CHECK(strcmp(first->end, "2026:08:23 08:30:00") == 0);
    CHECK(first->duration_seconds == 1800);
    CHECK(first->statistics.total_photos == 2 &&
          first->statistics.total_file_size == 900);
    CHECK(first->has_iso && first->iso_min == 100 && first->iso_max == 400);
    CHECK(first->statistics.focal_lengths.items[0].numeric_value == 9);
    CHECK(first->statistics.apertures.items[0].numeric_value == 4);
    CHECK(first->statistics.camera_models.count == 2);
    CHECK(strcmp(first->statistics.camera_models.items[0].value, "Camera A") ==
          0);
    CHECK(timeline.sessions[1].duration_seconds == 0 &&
          !timeline.sessions[1].has_iso);
    CHECK(strcmp(timeline.sessions[3].date, "2026-08-24") == 0);
    CHECK(photoc_timeline_finish(&timeline, 30) == 0);
    CHECK(timeline.session_count == 4); /* Exact 30-minute boundary joins. */
    CHECK(photoc_timeline_finish(&timeline, 0) == 0);
    CHECK(timeline.session_count == 5);
    add_photo(&timeline, "2026:08:23 08:00:00", "Camera B", 600, 24, 5.6, 200);
    CHECK(photoc_timeline_finish(&timeline, 0) == 0);
    CHECK(timeline.session_count ==
          5); /* Equal captures still join at zero gap. */
    CHECK(timeline.sessions[0].statistics.total_photos == 2);
    photoc_timeline_cleanup(&timeline);
    CHECK(timeline.photos == NULL && timeline.sessions == NULL &&
          timeline.session_count == 0);
    photoc_timeline_cleanup(&timeline);
}

static void test_errors(void)
{
    photoc_timeline timeline = {0};
    Photo photo = {0};
    CHECK(photoc_timeline_add(NULL, &photo) == -1 && errno == EINVAL);
    CHECK(photoc_timeline_add(&timeline, NULL) == -1 && errno == EINVAL);
    CHECK(photoc_timeline_finish(NULL, 60) == -1 && errno == EINVAL);
    timeline.skipped_missing_timestamp = UINT64_MAX;
    CHECK(photoc_timeline_add(&timeline, &photo) == -1 && errno == EOVERFLOW);
    photoc_timeline_cleanup(&timeline);
    add_photo(&timeline, "2026:08:23 08:00:00", "Cam", UINT64_MAX, 0, 0, 0);
    add_photo(&timeline, "2026:08:23 08:00:00", "Cam", 1, 0, 0, 0);
    CHECK(photoc_timeline_finish(&timeline, 60) == -1 && errno == EOVERFLOW);
    photoc_timeline_cleanup(&timeline);
    photoc_timeline_cleanup(NULL);
}

int main(void)
{
    test_grouping();
    test_errors();
    return failures == 0 ? 0 : 1;
}
