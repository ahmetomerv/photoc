#ifndef PHOTOC_REVIEW_MODEL_H
#define PHOTOC_REVIEW_MODEL_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    PHOTOC_REVIEW_UNMARKED,
    PHOTOC_REVIEW_PICKED,
    PHOTOC_REVIEW_REJECTED
} photoc_review_status;

typedef enum { PHOTOC_REVIEW_SORT_NAME, PHOTOC_REVIEW_SORT_DATE } review_sort;

typedef enum {
    PHOTOC_REVIEW_SHOW_ALL,
    PHOTOC_REVIEW_SHOW_UNMARKED,
    PHOTOC_REVIEW_SHOW_PICKED,
    PHOTOC_REVIEW_SHOW_REJECTED
} review_show;

typedef struct {
    char *path;              /* Owned path for reading the discovered JPEG. */
    char *relative_path;     /* Owned path relative to the review root. */
    char *capture_timestamp; /* Owned valid EXIF timestamp, or NULL. */
    photoc_review_status status;
} review_item;

typedef struct {
    review_item *items; /* Owns the array and all strings in each item. */
    size_t count;
    size_t *visible; /* Owns count slots of indices into items. */
    size_t visible_count;
    size_t cursor; /* Index in visible; zero when visible_count is zero. */
    size_t picked;
    size_t rejected;
    size_t unmarked;
    review_show show;
} review_model;

/* root is borrowed. out must be zero-initialized or cleaned. On success out
   owns every path and timestamp; on failure out is unchanged. Discovery uses
   only the filesystem walker. Date sorting loads metadata, never full pixels.
   Returns 0 or -1 with errno set. */
int review_model_load(review_model *out, const char *root, bool recursive,
                      review_sort sort, review_show show);

/* After restoring statuses into items, rebuild whole-collection counts and
   the startup filter once. Resets cursor to the first visible item. */
int review_model_rebuild(review_model *model);

/* Apply a mark only after the caller has persisted it. Counts are adjusted
   without rescanning. Advance to the next visible item; if the current item
   leaves the filter and was last, select the preceding item. */
int review_model_mark_current(review_model *model, photoc_review_status status);

const review_item *review_model_current(const review_model *model);
bool review_model_next(review_model *model);
bool review_model_previous(review_model *model);
void review_model_cleanup(review_model *model);

#endif
