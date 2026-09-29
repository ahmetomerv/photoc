#ifndef PHOTOC_PHOTO_H
#define PHOTOC_PHOTO_H

#include <stdbool.h>
#include <stdint.h>
#include "photoc/format.h"

typedef struct Photo {
    /* Required after photo_init. All strings are owned separately by Photo. */
    char *path;
    photoc_photo_format format;

    /* A zero value is not a missing marker; check each presence flag. */
    uint64_t file_size; /* bytes */
    bool has_file_size;
    uint32_t width; /* pixels */
    bool has_width;
    uint32_t height; /* pixels */
    bool has_height;
    uint16_t
        orientation; /* EXIF/TIFF orientation 1-8; pixels are not rotated. */
    bool has_orientation;

    /* NULL means unavailable. The timestamp is text with no assumed zone. */
    char *camera_make;
    char *camera_model;
    char *
        lens_model; /* Standard EXIF LensModel, never inferred from MakerNotes. */
    char *capture_timestamp;

    uint32_t iso;
    bool has_iso;
    double aperture; /* f-number */
    bool has_aperture;
    double exposure_time; /* seconds */
    bool has_exposure_time;
    double focal_length; /* millimeters */
    bool has_focal_length;
    uint32_t
        focal_length_35mm; /* Explicit EXIF 35mm equivalent, millimeters. */
    bool has_focal_length_35mm;

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

/* Result codes for photo_load_metadata; these are separate from CLI exit codes.
   IO_ERROR preserves errno from the failed filesystem operation. */
typedef enum {
    PHOTOC_METADATA_OK = 0,
    PHOTOC_METADATA_INVALID_ARGUMENT = 1,
    PHOTOC_METADATA_UNSUPPORTED_FORMAT = 2,
    PHOTOC_METADATA_INVALID_JPEG = 3,
    PHOTOC_METADATA_IO_ERROR = 4,
    PHOTOC_METADATA_NO_MEMORY = 5,
    PHOTOC_METADATA_INVALID_ARW = 6,
    PHOTOC_METADATA_RESOURCE_LIMIT = 7,
    PHOTOC_METADATA_PRIVACY_REMAINS = 8
} photoc_metadata_result;

/* Dispatches .jpg/.jpeg and Sony .arw to metadata-only backends (extensions
   are case-insensitive). JPEG frame dimensions are authoritative; ARW uses
   standard TIFF/EXIF tags. Missing or invalid field values stay unavailable.
   photo must be zero-initialized or cleaned before calling. On success it owns
   path and optional strings; call photo_cleanup. On failure it is unchanged.
   No diagnostic is printed by this layer. */
photoc_metadata_result photo_load_metadata(const char *path, Photo *photo);

/* Static, human-readable description of a metadata result. Never free it. */
const char *photo_metadata_result_message(photoc_metadata_result result);

#endif
