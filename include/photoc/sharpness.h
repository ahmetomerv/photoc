#ifndef PHOTOC_SHARPNESS_H
#define PHOTOC_SHARPNESS_H

#include "photoc/image.h"

#include <stdint.h>

#define PHOTOC_SHARPNESS_DEFAULT_MAX_DIMENSION 1024u

/* Score a JPEG by the population variance of a 4-neighbor Laplacian on
   grayscale pixels. Higher values mean more local edge variation. The JPEG
   is decoded at the largest supported scale within max_dimension, so scores
   can vary with analysis size. Use the same max_dimension for comparisons.
   The score is deterministic for a given file and decoder build. It is not
   an assessment of composition or artistic quality: texture, noise, JPEG
   artifacts, sharpening, and subject matter can raise the score. It cannot
   identify the intended subject or distinguish motion from defocus blur.
   No absolute sharp/blurred threshold is implied.

   max_dimension must be positive; 1024 is the recommended default. On
   success *score receives a nonnegative value. On error it is unchanged.
   No file is modified. All intermediate buffers are freed before return. */
photoc_image_result photoc_sharpness_score_jpeg(const char *path,
                                                uint32_t max_dimension,
                                                double *score);

/* As above for a JPEG or a Sony ARW. An ARW is scored from its embedded JPEG
   preview, area-averaged to the size its full-resolution image (EXIF/RAW
   dimensions) would be analyzed at, so ARW and JPEG scores of the same shot
   are comparable. Without recorded dimensions, or with a preview smaller than
   that size, the preview is scored at its largest scale within
   max_dimension. The preview carries in-camera processing, so scores stay a
   relative review aid. Errors include INVALID_ARW and NO_PREVIEW. */
photoc_image_result photoc_sharpness_score_photo(const char *path,
                                                 uint32_t max_dimension,
                                                 double *score);

#endif
