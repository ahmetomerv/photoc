#ifndef PHOTOC_JPEG_METADATA_H
#define PHOTOC_JPEG_METADATA_H

#include "photoc/jpeg_write.h"

/* Owned opaque snapshot of EXIF, standard/extended Adobe XMP APP1 segments,
   and ICC_PROFILE APP2 chunks, in source marker order. Payloads are retained
   byte for byte; no ICC conversion or XMP XML interpretation is performed.
   EXIF must pass the existing editable-EXIF validation. ICC numbering must be
   complete/consistent, with unique indices 1..count and nonempty total data.
   Recognized segments after the first scan are refused. Unknown APP markers
   and coding/scan markers are excluded. Snapshot allocations are capped at
   64 MiB (including segment bookkeeping) for untrusted inputs.
   On failure *out is NULL. On success even a metadata-free JPEG returns an
   owned snapshot containing its original dimensions. out must not already
   own memory. The caller releases the snapshot with metadata_free. */
photoc_jpeg_edit_result
photoc_jpeg_metadata_load_copy(const char *path, photoc_jpeg_metadata **out);

/* Exact number of bytes inserted into an encoded JPEG, including marker and
   length bytes. NULL has zero overhead. Does not transfer ownership. */
uint64_t
photoc_jpeg_metadata_output_overhead(const photoc_jpeg_metadata *metadata);

/* True only when ICC chunks belong to a one-component JPEG. Use grayscale
   encoding to retain that association; the ICC payload itself is not parsed. */
bool photoc_jpeg_metadata_has_grayscale_icc(
    const photoc_jpeg_metadata *metadata);

void photoc_jpeg_metadata_free(photoc_jpeg_metadata *metadata);

#endif
