#ifndef PHOTOC_TIMELINE_H
#define PHOTOC_TIMELINE_H

#include "photoc/photo.h"
#include "photoc/stats.h"

#include <stddef.h>
#include <stdint.h>

typedef struct photoc_timeline_photo photoc_timeline_photo;

typedef struct {
    char date[11];  /* YYYY-MM-DD, recorded calendar date, no zone inferred. */
    char start[20]; /* Full local EXIF timestamps, YYYY:MM:DD HH:MM:SS. */
    char end[20];
    uint64_t duration_seconds;
    photoc_stats_aggregate statistics; /* Owns buckets and scalar samples. */
    bool has_iso;
    uint32_t iso_min;
    uint32_t iso_max;
} photoc_timeline_session;

typedef struct {
    photoc_timeline_photo *photos; /* Owned compact metadata, no pixel data. */
    size_t photo_count;
    size_t photo_capacity;
    photoc_timeline_session *sessions; /* Owned, in chronological order. */
    size_t session_count;
    size_t session_capacity;
    size_t date_count;
    uint64_t skipped_missing_timestamp;
    uint64_t skipped_invalid_timestamp;
} photoc_timeline;

/* Initialize a fresh report; cleanup releases all retained strings, samples,
   arrays and sessions. Cleanup accepts NULL or a previously cleaned report. */
void photoc_timeline_init(photoc_timeline *timeline);
void photoc_timeline_cleanup(photoc_timeline *timeline);

/* Copies only metadata needed for the report from a borrowed Photo. No Photo
   or borrowed string is retained. Missing/invalid timestamps are counted and
   excluded. Returns 0 or -1 with errno on invalid args, OOM or overflow. On
   failure the report remains safe to clean; the caller must abort. */
int photoc_timeline_add(photoc_timeline *timeline, const Photo *photo);

/* Sorts by recorded capture time, groups by date then shared session-gap rule,
   and aggregates through core stats. An exact gap joins within a date; midnight
   always splits. May be repeated, or called after further adds. Returns 0 or
   -1 with errno; no output is written. Report owns every resulting session. */
int photoc_timeline_finish(photoc_timeline *timeline, uint32_t gap_minutes);

#endif
