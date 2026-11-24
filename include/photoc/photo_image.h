#ifndef PHOTOC_PHOTO_IMAGE_H
#define PHOTOC_PHOTO_IMAGE_H

#include <stdint.h>

#include "photoc/image.h"

/* Decode a JPEG, or a Sony ARW's embedded JPEG preview, at the largest
   TurboJPEG scale within max_dimension (positive). max_source_bytes (positive)
   caps the compressed JPEG bytes read: the JPEG file itself, or the ARW's
   preview, never the whole ARW. EXIF orientation is not applied. decoder may
   be NULL. Any path without an .arw extension is decoded as JPEG, whose
   content the decoder validates. out must be empty;
   on success it owns pixels (photoc_image_cleanup), on error it stays empty. */
photoc_image_result photoc_photo_decode_scaled_bounded_with(
    photoc_image_decoder *decoder, const char *path, uint32_t max_dimension,
    uint64_t max_source_bytes, photoc_image *out);

#endif
