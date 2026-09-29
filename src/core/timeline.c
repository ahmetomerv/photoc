#include "photoc/timeline.h"
#include "photoc/session.h"
#include "photoc/timestamp.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct photoc_timeline_photo {
    char captured[20];
    uint64_t seconds;
    uint64_t file_size;
    char *camera_model; /* Owned; other source strings are not retained. */
    double focal_length;
    double aperture;
    uint32_t iso;
    bool has_file_size;
    bool has_focal_length;
    bool has_aperture;
    bool has_iso;
};

void photoc_timeline_init(photoc_timeline *timeline)
{
    if (timeline != NULL)
        *timeline = (photoc_timeline){0};
}

static void clear_sessions(photoc_timeline *timeline)
{
    for (size_t i = 0; i < timeline->session_count; ++i)
        photoc_stats_cleanup(&timeline->sessions[i].statistics);
    free(timeline->sessions);
    timeline->sessions = NULL;
    timeline->session_count = 0;
    timeline->session_capacity = 0;
    timeline->date_count = 0;
}

void photoc_timeline_cleanup(photoc_timeline *timeline)
{
    if (timeline == NULL)
        return;
    clear_sessions(timeline);
    for (size_t i = 0; i < timeline->photo_count; ++i)
        free(timeline->photos[i].camera_model);
    free(timeline->photos);
    *timeline = (photoc_timeline){0};
}

int photoc_timeline_add(photoc_timeline *timeline, const Photo *photo)
{
    if (timeline == NULL || photo == NULL) {
        errno = EINVAL;
        return -1;
    }
    uint64_t seconds;
    if (!photoc_timestamp_to_seconds(photo->capture_timestamp, &seconds)) {
        uint64_t *skipped = photo->capture_timestamp == NULL ||
                                    photo->capture_timestamp[0] == '\0'
                                ? &timeline->skipped_missing_timestamp
                                : &timeline->skipped_invalid_timestamp;
        if (*skipped == UINT64_MAX) {
            errno = EOVERFLOW;
            return -1;
        }
        ++*skipped;
        return 0;
    }
    if (timeline->photo_count == timeline->photo_capacity) {
        size_t capacity =
            timeline->photo_capacity == 0 ? 32 : timeline->photo_capacity * 2;
        if (capacity < timeline->photo_capacity ||
            capacity > SIZE_MAX / sizeof(*timeline->photos)) {
            errno = EOVERFLOW;
            return -1;
        }
        photoc_timeline_photo *photos =
            realloc(timeline->photos, capacity * sizeof(*photos));
        if (photos == NULL)
            return -1;
        timeline->photos = photos;
        timeline->photo_capacity = capacity;
    }
    photoc_timeline_photo retained = {.seconds = seconds,
                                      .file_size = photo->file_size,
                                      .has_file_size = photo->has_file_size,
                                      .focal_length = photo->focal_length,
                                      .has_focal_length =
                                          photo->has_focal_length,
                                      .aperture = photo->aperture,
                                      .has_aperture = photo->has_aperture,
                                      .iso = photo->iso,
                                      .has_iso = photo->has_iso};
    memcpy(retained.captured, photo->capture_timestamp,
           sizeof(retained.captured));
    if (photo->camera_model != NULL && photo->camera_model[0] != '\0') {
        size_t length = strlen(photo->camera_model);
        if (length == SIZE_MAX) {
            errno = EOVERFLOW;
            return -1;
        }
        retained.camera_model = malloc(length + 1);
        if (retained.camera_model == NULL)
            return -1;
        memcpy(retained.camera_model, photo->camera_model, length + 1);
    }
    timeline->photos[timeline->photo_count++] = retained;
    return 0;
}

static int compare_photos(const void *left, const void *right)
{
    const photoc_timeline_photo *a = left, *b = right;
    return a->seconds < b->seconds ? -1 : a->seconds > b->seconds ? 1 : 0;
}

static photoc_timeline_session *
start_session(photoc_timeline *timeline, const photoc_timeline_photo *photo)
{
    if (timeline->session_count == timeline->session_capacity) {
        size_t capacity = timeline->session_capacity == 0
                              ? 8
                              : timeline->session_capacity * 2;
        if (capacity < timeline->session_capacity ||
            capacity > SIZE_MAX / sizeof(*timeline->sessions)) {
            errno = EOVERFLOW;
            return NULL;
        }
        photoc_timeline_session *sessions =
            realloc(timeline->sessions, capacity * sizeof(*sessions));
        if (sessions == NULL)
            return NULL;
        timeline->sessions = sessions;
        timeline->session_capacity = capacity;
    }
    photoc_timeline_session *session =
        &timeline->sessions[timeline->session_count++];
    *session = (photoc_timeline_session){0};
    memcpy(session->start, photo->captured, sizeof(session->start));
    memcpy(session->date, photo->captured, 10);
    session->date[4] = '-';
    session->date[7] = '-';
    session->date[10] = '\0';
    if (timeline->session_count == 1 ||
        strcmp(session->date,
               timeline->sessions[timeline->session_count - 2].date) != 0)
        ++timeline->date_count;
    return session;
}

int photoc_timeline_finish(photoc_timeline *timeline, uint32_t gap_minutes)
{
    if (timeline == NULL) {
        errno = EINVAL;
        return -1;
    }
    clear_sessions(timeline);
    if (timeline->photo_count > 1)
        qsort(timeline->photos, timeline->photo_count,
              sizeof(*timeline->photos), compare_photos);
    photoc_timeline_session *session = NULL;
    uint64_t start_seconds = 0;
    for (size_t i = 0; i < timeline->photo_count; ++i) {
        const photoc_timeline_photo *photo = &timeline->photos[i];
        bool new_session =
            i == 0 ||
            memcmp(photo->captured, timeline->photos[i - 1].captured, 10) !=
                0 ||
            photoc_session_starts_new(timeline->photos[i - 1].seconds,
                                      photo->seconds, gap_minutes);
        if (new_session) {
            session = start_session(timeline, photo);
            if (session == NULL)
                return -1;
            start_seconds = photo->seconds;
        }
        /* Reuse stats only for fields in this overview. Timestamps are already
           grouped; omitted projection fields never allocate unrelated buckets. */
        Photo projection = {.camera_model = photo->camera_model,
                            .file_size = photo->file_size,
                            .has_file_size = photo->has_file_size,
                            .focal_length = photo->focal_length,
                            .has_focal_length = photo->has_focal_length,
                            .aperture = photo->aperture,
                            .has_aperture = photo->has_aperture,
                            .iso = photo->iso,
                            .has_iso = photo->has_iso};
        if (photoc_stats_add_photo(&session->statistics, &projection) != 0)
            return -1;
        memcpy(session->end, photo->captured, sizeof(session->end));
        session->duration_seconds = photo->seconds - start_seconds;
    }
    for (size_t i = 0; i < timeline->session_count; ++i) {
        session = &timeline->sessions[i];
        photoc_stats_sort(&session->statistics);
        for (size_t j = 0; j < session->statistics.iso_values.count; ++j) {
            uint32_t iso =
                (uint32_t)session->statistics.iso_values.items[j].numeric_value;
            if (!session->has_iso || iso < session->iso_min)
                session->iso_min = iso;
            if (!session->has_iso || iso > session->iso_max)
                session->iso_max = iso;
            session->has_iso = true;
        }
    }
    return 0;
}
