#ifndef PHOTOC_CORE_JPEG_H
#define PHOTOC_CORE_JPEG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint64_t scan_offset; /* First SOS marker, including its 0xff byte. */
    uint64_t exif_insert_offset; /* After leading APP0 segments, if present. */
    uint64_t exif_offset; /* First EXIF APP1 marker, including its 0xff byte. */
    uint64_t exif_length; /* Complete marker, length field, and payload. */
    unsigned int exif_count;
    bool exif_after_scan;
} photoc_jpeg_info;

/* Inspects a seekable JPEG stream without decoding image data. Returns 0 on
   success, -1 with errno EINVAL for malformed JPEG or EIO for a read error.
   The stream is left near EOI. */
int photoc_jpeg_inspect(FILE *file, photoc_jpeg_info *info);

#endif
