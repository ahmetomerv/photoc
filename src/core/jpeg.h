#ifndef PHOTOC_CORE_JPEG_H
#define PHOTOC_CORE_JPEG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    unsigned int components;
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

/* Optional APP1/APP2 visitor. Payload is borrowed only during the callback.
   Return nonzero to stop inspection; the visitor's errno is preserved. */
typedef int (*photoc_jpeg_app_visitor)(unsigned char marker,
                                       const unsigned char *payload,
                                       unsigned int length, bool after_scan,
                                       void *user_data);
int photoc_jpeg_inspect_app(FILE *file, photoc_jpeg_info *info,
                            photoc_jpeg_app_visitor visitor, void *user_data);

#endif
