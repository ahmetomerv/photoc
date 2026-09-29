#ifndef PHOTOC_FORMAT_H
#define PHOTOC_FORMAT_H

#include <stdbool.h>

typedef enum {
    PHOTOC_FORMAT_UNKNOWN,
    PHOTOC_FORMAT_JPEG,
    PHOTOC_FORMAT_SONY_ARW
} photoc_photo_format;

#define PHOTOC_FORMATS_JPEG (1u << PHOTOC_FORMAT_JPEG)
#define PHOTOC_FORMATS_METADATA                                                \
    (PHOTOC_FORMATS_JPEG | (1u << PHOTOC_FORMAT_SONY_ARW))

/* Case-insensitive extension discovery only; the backend validates content.
   No file is opened and all strings are borrowed. Other RAW types are unknown. */
photoc_photo_format photoc_format_from_path(const char *path);
const char *photoc_format_name(photoc_photo_format format);
bool photoc_format_is_selected(const char *path, unsigned int formats);

#endif
