#ifndef PHOTOC_TIFF_H
#define PHOTOC_TIFF_H

#include <libexif/exif-utils.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define PHOTOC_TIFF_MAX_DIRECTORIES 64u
#define PHOTOC_TIFF_MAX_ENTRIES 16384u
#define PHOTOC_TIFF_READ_BUDGET UINT64_C(2097152)
#define PHOTOC_TIFF_MAX_VALUE 65536u

typedef enum {
    PHOTOC_TIFF_IMAGE,
    PHOTOC_TIFF_EXIF,
    PHOTOC_TIFF_GPS,
    PHOTOC_TIFF_INTEROP
} photoc_tiff_directory_kind;

typedef struct {
    FILE *file; /* Borrowed; no ownership escapes this reader. */
    uint64_t size;
    uint64_t bytes_read;
    ExifByteOrder order;
} photoc_tiff_reader;

typedef struct {
    uint16_t tag;
    uint16_t type;
    uint32_t count;
    uint64_t size;
    uint64_t
        offset; /* All values, including inline ones, reference the file. */
    uint32_t directory_offset;
    bool primary; /* IFD0, excluding preview/other image directories. */
    photoc_tiff_directory_kind kind;
} photoc_tiff_entry;

/* Caller chooses values to read; callback arguments are borrowed. All entries'
   declared ranges are validated, including unselected values, but their bytes
   are not read. Return false with errno set to abort. No recursion/heap arrays. */
typedef bool (*photoc_tiff_visit_fn)(photoc_tiff_reader *reader,
                                     const photoc_tiff_entry *entry,
                                     void *data);

/* Classic TIFF (magic 42), either endian; rejects BigTIFF and malformed graphs.
   Follows standard next, SubIFD, EXIF/GPS/interop links with fixed bounds.
   Returns 0 or -1 with EINVAL (malformed), EFBIG (limits), or I/O errno.
   Caller owns FILE and callback data; no image/thumbnail/codec processing. */
int photoc_tiff_walk(FILE *file, uint64_t size, photoc_tiff_visit_fn visit,
                     void *data);

/* Bound every read by the original file size and aggregate metadata budget.
   destination must have room for size bytes; no allocation is performed. */
bool photoc_tiff_read(photoc_tiff_reader *reader, uint64_t offset,
                      unsigned char *destination, size_t size);

#endif
