#include "photoc/sharpness.h"

#include "photoc/arw.h"
#include "photoc/fs.h"
#include "photoc/image_analysis.h"
#include "photoc/photo.h"

#include <stdlib.h>

static photoc_image_result analysis_error(photoc_analysis_result result)
{
    switch (result) {
    case PHOTOC_ANALYSIS_TOO_LARGE:
        return PHOTOC_IMAGE_TOO_LARGE;
    case PHOTOC_ANALYSIS_NO_MEMORY:
        return PHOTOC_IMAGE_NO_MEMORY;
    case PHOTOC_ANALYSIS_OK:
        return PHOTOC_IMAGE_OK;
    case PHOTOC_ANALYSIS_INVALID_ARGUMENT:
    default:
        return PHOTOC_IMAGE_INVALID_ARGUMENT;
    }
}

/* Consumes image (it is cleaned up before return). */
static photoc_image_result score_image(photoc_image *image, double *score)
{
    photoc_gray_image gray = {0};
    photoc_analysis_result analysis = photoc_gray_from_rgb(image, 1, &gray);
    photoc_image_cleanup(image);
    if (analysis != PHOTOC_ANALYSIS_OK) {
        return analysis_error(analysis);
    }

    photoc_laplacian_image laplacian = {0};
    analysis = photoc_gray_laplacian(&gray, &laplacian);
    photoc_gray_cleanup(&gray);
    if (analysis != PHOTOC_ANALYSIS_OK) {
        return analysis_error(analysis);
    }

    double measured = 0.0;
    analysis =
        photoc_variance_i16(laplacian.values, laplacian.value_count, &measured);
    photoc_laplacian_cleanup(&laplacian);
    if (analysis != PHOTOC_ANALYSIS_OK) {
        return analysis_error(analysis);
    }
    *score = measured;
    return PHOTOC_IMAGE_OK;
}

photoc_image_result photoc_sharpness_score_jpeg(const char *path,
                                                uint32_t max_dimension,
                                                double *score)
{
    if (path == NULL || path[0] == '\0' || max_dimension == 0 ||
        score == NULL) {
        return PHOTOC_IMAGE_INVALID_ARGUMENT;
    }

    photoc_image image = {0};
    photoc_image_result result =
        photoc_image_decode_jpeg_scaled(path, max_dimension, &image);
    if (result != PHOTOC_IMAGE_OK) {
        return result;
    }
    return score_image(&image, score);
}

/* The size decode_jpeg_scaled would choose for the full-resolution image this
   ARW records, so preview scores are comparable with camera JPEG scores. */
static photoc_image_result arw_analysis_size(const char *path,
                                             uint32_t max_dimension,
                                             uint32_t *width, uint32_t *height,
                                             bool *matched)
{
    Photo photo = {0};
    photoc_metadata_result metadata = photo_load_metadata(path, &photo);
    if (metadata == PHOTOC_METADATA_IO_ERROR) {
        return PHOTOC_IMAGE_IO_ERROR;
    }
    if (metadata == PHOTOC_METADATA_NO_MEMORY) {
        return PHOTOC_IMAGE_NO_MEMORY;
    }
    if (metadata != PHOTOC_METADATA_OK) {
        return PHOTOC_IMAGE_INVALID_ARW;
    }
    *matched =
        photo.has_width && photo.has_height &&
        photoc_image_scaled_dimensions(photo.width, photo.height, max_dimension,
                                       width, height) == PHOTOC_IMAGE_OK;
    photo_cleanup(&photo);
    return PHOTOC_IMAGE_OK;
}

static photoc_image_result score_arw(const char *path, uint32_t max_dimension,
                                     double *score)
{
    uint32_t width = 0;
    uint32_t height = 0;
    bool matched = false;
    photoc_image_result result =
        arw_analysis_size(path, max_dimension, &width, &height, &matched);
    unsigned char *preview = NULL;
    size_t length = 0;
    if (result == PHOTOC_IMAGE_OK) {
        result = photoc_arw_read_preview(path, 0, &preview, &length);
    }
    photoc_image image = {0};
    if (result == PHOTOC_IMAGE_OK) {
        result = photoc_image_decode_jpeg_buffer_scaled_with(
            NULL, preview, length, matched ? UINT32_MAX : max_dimension,
            &image);
        free(preview);
    }
    if (result != PHOTOC_IMAGE_OK) {
        return result;
    }
    /* A smaller preview is analyzed as is rather than upscaled. */
    if (matched && image.width > width && image.height > height) {
        photoc_image resized = {0};
        result = photoc_image_resize_area(&image, width, height, &resized);
        photoc_image_cleanup(&image);
        if (result != PHOTOC_IMAGE_OK) {
            return result;
        }
        image = resized;
    }
    return score_image(&image, score);
}

photoc_image_result photoc_sharpness_score_photo(const char *path,
                                                 uint32_t max_dimension,
                                                 double *score)
{
    if (path == NULL || path[0] == '\0' || max_dimension == 0 ||
        score == NULL) {
        return PHOTOC_IMAGE_INVALID_ARGUMENT;
    }
    if (photoc_fs_is_arw(path)) {
        return score_arw(path, max_dimension, score);
    }
    return photoc_sharpness_score_jpeg(path, max_dimension, score);
}
