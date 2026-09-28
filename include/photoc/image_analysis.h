#ifndef PHOTOC_IMAGE_ANALYSIS_H
#define PHOTOC_IMAGE_ANALYSIS_H

#include "photoc/image.h"

#include <stddef.h>
#include <stdint.h>

/* Each analysis buffer is limited to this many pixels. Holding a grayscale
   and Laplacian image together uses at most 3 * MAX_PIXELS bytes. */
#define PHOTOC_ANALYSIS_MAX_PIXELS ((size_t)16 * 1024 * 1024)

typedef enum {
    PHOTOC_ANALYSIS_OK = 0,
    PHOTOC_ANALYSIS_INVALID_ARGUMENT,
    PHOTOC_ANALYSIS_TOO_LARGE,
    PHOTOC_ANALYSIS_NO_MEMORY
} photoc_analysis_result;

/* Contiguous, row-major 8-bit grayscale pixels. Owned by this struct;
   release with photoc_gray_cleanup. */
typedef struct {
    uint32_t width;
    uint32_t height;
    size_t pixel_count;
    uint8_t *pixels;
} photoc_gray_image;

/* Contiguous, row-major signed 4-neighbor Laplacian values. The first and
   last row and column are zero because they lack all four neighbors.
   Owned by this struct; release with photoc_laplacian_cleanup. */
typedef struct {
    uint32_t width;
    uint32_t height;
    size_t value_count;
    int16_t *values;
} photoc_laplacian_image;

/* Convert decoded RGB to grayscale using rounded integer Rec. 601 luma:
   (299R + 587G + 114B + 500) / 1000. sample_step=1 keeps every pixel; larger
   steps sample the top-left pixel of each step-sized grid cell. The output
   dimensions are ceil(input dimension / sample_step). The caller owns the
   result and must pass a zero-initialized or cleaned output struct. On error,
   the output is unchanged. The input remains borrowed and unchanged. */
photoc_analysis_result photoc_gray_from_rgb(const photoc_image *input,
                                            uint32_t sample_step,
                                            photoc_gray_image *output);

/* Compute 4*center - left - right - above - below at interior pixels.
   Outputs zero at borders, including all pixels in images smaller than 3x3.
   The caller owns the result and must pass a zero-initialized or cleaned
   output struct. On error, the output is unchanged. */
photoc_analysis_result photoc_gray_laplacian(const photoc_gray_image *input,
                                             photoc_laplacian_image *output);

/* Population variance of count signed values using a stable one-pass mean.
   Requires count > 0; does not allocate memory. Output is unchanged on error. */
photoc_analysis_result photoc_variance_i16(const int16_t *values, size_t count,
                                           double *variance);

void photoc_gray_cleanup(photoc_gray_image *image);
void photoc_laplacian_cleanup(photoc_laplacian_image *image);

/* Static diagnostic string; never free it. */
const char *photoc_analysis_result_message(photoc_analysis_result result);

#endif
