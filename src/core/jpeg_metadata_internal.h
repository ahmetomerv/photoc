#ifndef PHOTOC_JPEG_METADATA_INTERNAL_H
#define PHOTOC_JPEG_METADATA_INTERNAL_H

#include "photoc/jpeg_metadata.h"

#include <stdio.h>

/* Core-only helpers. Arguments/streams are borrowed for the synchronous call.
   The EXIF validator shares scrub's parser but never edits metadata. */
photoc_jpeg_edit_result
photoc_jpeg_exif_validate_segment(const unsigned char *bytes,
                                  unsigned int length);
photoc_jpeg_edit_result
photoc_jpeg_metadata_write_encoded(FILE *file,
                                   const photoc_jpeg_buffer *encoded,
                                   const photoc_jpeg_metadata *metadata);
photoc_jpeg_edit_result
photoc_jpeg_metadata_verify(const photoc_jpeg_metadata *metadata,
                            const char *path);

#endif
