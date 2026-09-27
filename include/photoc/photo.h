#ifndef PHOTOC_PHOTO_H
#define PHOTOC_PHOTO_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Photo {
    /* Required after photo_init. All strings are owned separately by Photo. */
    char *path;

    /* A zero value is not a missing marker; check each presence flag. */
    uint64_t file_size;       /* bytes */
    bool has_file_size;
    uint32_t width;           /* pixels */
    bool has_width;
    uint32_t height;          /* pixels */
    bool has_height;

    /* NULL means unavailable. The timestamp is text with no assumed zone. */
    char *camera_make;
    char *camera_model;
    char *capture_timestamp;

    uint32_t iso;
    bool has_iso;
    double aperture;          /* f-number */
    bool has_aperture;
    double exposure_time;     /* seconds */
    bool has_exposure_time;
    double focal_length;      /* millimeters */
    bool has_focal_length;

    /* Coordinates are decimal degrees and valid only when has_gps is true. */
    bool has_gps;
    double latitude;
    double longitude;
} Photo;

/* Initializes an uninitialized or zeroed Photo with an owned copy of path.
   Returns 0 on success or -1 with errno set. On failure, Photo is unchanged.
   Call photo_cleanup before initializing the same object again. */
int photo_init(Photo *photo, const char *path);

/* Frees path and all optional strings, then zeroes the struct. Optional
   strings assigned later must each be separately malloc-allocated; do not
   assign string literals or share one allocation between fields. Safe for
   NULL and for a zeroed or previously cleaned Photo. */
void photo_cleanup(Photo *photo);

#endif
