#ifndef PHOTOC_METADATA_INTERNAL_H
#define PHOTOC_METADATA_INTERNAL_H

#include "photoc/photo.h"
#include <libexif/exif-data.h>
#include <stdio.h>

/* Extract common fields from borrowed libexif entries into a fresh Photo.
   Photo owns allocated strings. Caller retains and frees ExifData. */
bool photoc_metadata_read_exif(ExifData *data, Photo *photo);

/* Populate a fresh Photo from a borrowed, opened regular ARW stream and size.
   On failure the caller cleans up any partial Photo. No file/Photo is retained. */
photoc_metadata_result photoc_arw_load_metadata(FILE *file, uint64_t size,
                                                Photo *photo);
#endif
