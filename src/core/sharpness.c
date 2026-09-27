#include "photoc/sharpness.h"

#include "photoc/image_analysis.h"

static photoc_image_result analysis_error(photoc_analysis_result result)
{
    switch (result) {
    case PHOTOC_ANALYSIS_TOO_LARGE: return PHOTOC_IMAGE_TOO_LARGE;
    case PHOTOC_ANALYSIS_NO_MEMORY: return PHOTOC_IMAGE_NO_MEMORY;
    case PHOTOC_ANALYSIS_OK: return PHOTOC_IMAGE_OK;
    case PHOTOC_ANALYSIS_INVALID_ARGUMENT:
    default:
        return PHOTOC_IMAGE_INVALID_ARGUMENT;
    }
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
    photoc_image_result result = photoc_image_decode_jpeg_scaled(
        path, max_dimension, &image);
    if (result != PHOTOC_IMAGE_OK) {
        return result;
    }

    photoc_gray_image gray = {0};
    photoc_analysis_result analysis = photoc_gray_from_rgb(&image, 1, &gray);
    photoc_image_cleanup(&image);
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
    analysis = photoc_variance_i16(laplacian.values,
                                   laplacian.value_count, &measured);
    photoc_laplacian_cleanup(&laplacian);
    if (analysis != PHOTOC_ANALYSIS_OK) {
        return analysis_error(analysis);
    }
    *score = measured;
    return PHOTOC_IMAGE_OK;
}
