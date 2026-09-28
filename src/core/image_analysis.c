#include "photoc/image_analysis.h"

#include <stdlib.h>

void photoc_gray_cleanup(photoc_gray_image *image)
{
    if (image != NULL) {
        free(image->pixels);
        *image = (photoc_gray_image){0};
    }
}

void photoc_laplacian_cleanup(photoc_laplacian_image *image)
{
    if (image != NULL) {
        free(image->values);
        *image = (photoc_laplacian_image){0};
    }
}

photoc_analysis_result photoc_gray_from_rgb(const photoc_image *input,
                                            uint32_t sample_step,
                                            photoc_gray_image *output)
{
    if (input == NULL || output == NULL || output->pixels != NULL ||
        output->width != 0 || output->height != 0 || output->pixel_count != 0 ||
        sample_step == 0 || input->pixels == NULL || input->width == 0 ||
        input->height == 0) {
        return PHOTOC_ANALYSIS_INVALID_ARGUMENT;
    }
#if SIZE_MAX / 3 < UINT32_MAX
    if (input->width > SIZE_MAX / 3) {
        return PHOTOC_ANALYSIS_TOO_LARGE;
    }
#endif
    size_t row_bytes = (size_t)input->width * 3;
    if (input->stride < row_bytes || input->height > SIZE_MAX / input->stride ||
        input->pixel_bytes < input->stride * (size_t)input->height) {
        return PHOTOC_ANALYSIS_INVALID_ARGUMENT;
    }
    uint32_t width = (input->width - 1) / sample_step + 1;
    uint32_t height = (input->height - 1) / sample_step + 1;
    if (width > PHOTOC_ANALYSIS_MAX_PIXELS / height) {
        return PHOTOC_ANALYSIS_TOO_LARGE;
    }
    size_t count = (size_t)width * height;
    uint8_t *pixels = malloc(count);
    if (pixels == NULL) {
        return PHOTOC_ANALYSIS_NO_MEMORY;
    }
    for (uint32_t y = 0; y < height; ++y) {
        size_t source_row = (size_t)y * sample_step * input->stride;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t *rgb =
                input->pixels + source_row + (size_t)x * sample_step * 3;
            unsigned int weighted =
                299u * rgb[0] + 587u * rgb[1] + 114u * rgb[2];
            pixels[(size_t)y * width + x] =
                (uint8_t)((weighted + 500u) / 1000u);
        }
    }
    *output = (photoc_gray_image){width, height, count, pixels};
    return PHOTOC_ANALYSIS_OK;
}

photoc_analysis_result photoc_gray_laplacian(const photoc_gray_image *input,
                                             photoc_laplacian_image *output)
{
    if (input == NULL || output == NULL || output->values != NULL ||
        output->width != 0 || output->height != 0 || output->value_count != 0 ||
        input->pixels == NULL || input->width == 0 || input->height == 0 ||
        input->width > SIZE_MAX / input->height ||
        input->pixel_count != (size_t)input->width * input->height) {
        return PHOTOC_ANALYSIS_INVALID_ARGUMENT;
    }
    size_t count = input->pixel_count;
    if (count > PHOTOC_ANALYSIS_MAX_PIXELS ||
        count > SIZE_MAX / sizeof(int16_t)) {
        return PHOTOC_ANALYSIS_TOO_LARGE;
    }
    int16_t *values = calloc(count, sizeof(*values));
    if (values == NULL) {
        return PHOTOC_ANALYSIS_NO_MEMORY;
    }
    if (input->width >= 3 && input->height >= 3) {
        for (uint32_t y = 1; y < input->height - 1; ++y) {
            for (uint32_t x = 1; x < input->width - 1; ++x) {
                size_t index = (size_t)y * input->width + x;
                int center = input->pixels[index];
                int laplacian = 4 * center - input->pixels[index - 1] -
                                input->pixels[index + 1] -
                                input->pixels[index - input->width] -
                                input->pixels[index + input->width];
                values[index] = (int16_t)laplacian;
            }
        }
    }
    *output =
        (photoc_laplacian_image){input->width, input->height, count, values};
    return PHOTOC_ANALYSIS_OK;
}

photoc_analysis_result photoc_variance_i16(const int16_t *values, size_t count,
                                           double *variance)
{
    if (values == NULL || count == 0 || variance == NULL) {
        return PHOTOC_ANALYSIS_INVALID_ARGUMENT;
    }
    double mean = 0.0;
    double sum_squared = 0.0;
    for (size_t i = 0; i < count; ++i) {
        double delta = (double)values[i] - mean;
        mean += delta / (double)(i + 1);
        sum_squared += delta * ((double)values[i] - mean);
    }
    *variance = sum_squared / (double)count;
    return PHOTOC_ANALYSIS_OK;
}

const char *photoc_analysis_result_message(photoc_analysis_result result)
{
    switch (result) {
    case PHOTOC_ANALYSIS_OK:
        return "analysis completed";
    case PHOTOC_ANALYSIS_INVALID_ARGUMENT:
        return "invalid analysis argument";
    case PHOTOC_ANALYSIS_TOO_LARGE:
        return "sampled image is too large";
    case PHOTOC_ANALYSIS_NO_MEMORY:
        return "out of memory";
    default:
        return "unknown analysis error";
    }
}
