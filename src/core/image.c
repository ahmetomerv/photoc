#define _POSIX_C_SOURCE 200809L

#include "photoc/image.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <turbojpeg.h>

void photoc_image_cleanup(photoc_image *image)
{
    if (image != NULL) {
        free(image->pixels);
        *image = (photoc_image){0};
    }
}

void photoc_jpeg_buffer_cleanup(photoc_jpeg_buffer *buffer)
{
    if (buffer != NULL) {
        if (buffer->data != NULL) {
            tjFree(buffer->data);
        }
        *buffer = (photoc_jpeg_buffer){0};
    }
}

static photoc_image_result read_file(const char *path, unsigned char **bytes,
                                     unsigned long *length,
                                     uint64_t max_file_bytes)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return PHOTOC_IMAGE_IO_ERROR;
    }
    photoc_image_result result = PHOTOC_IMAGE_OK;
    if (fseeko(file, 0, SEEK_END) != 0) {
        result = PHOTOC_IMAGE_IO_ERROR;
    }
    off_t size = result == PHOTOC_IMAGE_OK ? ftello(file) : -1;
    if (result == PHOTOC_IMAGE_OK && size < 0) {
        result = PHOTOC_IMAGE_IO_ERROR;
    } else if (result == PHOTOC_IMAGE_OK && size == 0) {
        result = PHOTOC_IMAGE_INVALID_JPEG;
    } else if (result == PHOTOC_IMAGE_OK && max_file_bytes != 0 &&
               (uintmax_t)size > max_file_bytes) {
        result = PHOTOC_IMAGE_TOO_LARGE;
    } else if (result == PHOTOC_IMAGE_OK &&
               ((uintmax_t)size > SIZE_MAX || (uintmax_t)size > ULONG_MAX)) {
        result = PHOTOC_IMAGE_TOO_LARGE;
    }
    if (result == PHOTOC_IMAGE_OK && fseeko(file, 0, SEEK_SET) != 0) {
        result = PHOTOC_IMAGE_IO_ERROR;
    }

    unsigned char *data = NULL;
    if (result == PHOTOC_IMAGE_OK) {
        data = malloc((size_t)size);
        if (data == NULL) {
            result = PHOTOC_IMAGE_NO_MEMORY;
        }
    }
    if (result == PHOTOC_IMAGE_OK) {
        errno = 0;
    }
    if (result == PHOTOC_IMAGE_OK &&
        fread(data, 1, (size_t)size, file) != (size_t)size) {
        if (errno == 0) {
            errno = EIO;
        }
        result = PHOTOC_IMAGE_IO_ERROR;
    }
    int saved_errno = errno;
    if (fclose(file) != 0 && result == PHOTOC_IMAGE_OK) {
        saved_errno = errno;
        result = PHOTOC_IMAGE_IO_ERROR;
    }
    if (result != PHOTOC_IMAGE_OK) {
        free(data);
        errno = saved_errno;
        return result;
    }
    *bytes = data;
    *length = (unsigned long)size;
    return PHOTOC_IMAGE_OK;
}

static photoc_image_result read_header(tjhandle handle,
                                       const unsigned char *bytes,
                                       unsigned long length, int *width,
                                       int *height)
{
    int subsampling;
    int colorspace;
    if (tjDecompressHeader3(handle, bytes, length, width, height, &subsampling,
                            &colorspace) != 0 ||
        *width <= 0 || *height <= 0) {
        return PHOTOC_IMAGE_INVALID_JPEG;
    }
    return PHOTOC_IMAGE_OK;
}

photoc_image_result photoc_image_jpeg_dimensions(const char *path,
                                                 uint32_t *width,
                                                 uint32_t *height)
{
    if (path == NULL || path[0] == '\0' || width == NULL || height == NULL) {
        return PHOTOC_IMAGE_INVALID_ARGUMENT;
    }
    unsigned char *bytes = NULL;
    unsigned long length = 0;
    photoc_image_result result = read_file(path, &bytes, &length, 0);
    if (result != PHOTOC_IMAGE_OK) {
        return result;
    }
    tjhandle handle = tjInitDecompress();
    if (handle == NULL) {
        free(bytes);
        return PHOTOC_IMAGE_CODEC_ERROR;
    }
    int jpeg_width = 0;
    int jpeg_height = 0;
    result = read_header(handle, bytes, length, &jpeg_width, &jpeg_height);
    if (result == PHOTOC_IMAGE_OK) {
        *width = (uint32_t)jpeg_width;
        *height = (uint32_t)jpeg_height;
    }
    tjDestroy(handle);
    free(bytes);
    return result;
}

static photoc_image_result decode_jpeg(const char *path, uint32_t max_dimension,
                                       uint64_t max_file_bytes,
                                       photoc_image *out)
{
    if (path == NULL || path[0] == '\0' || out == NULL) {
        return PHOTOC_IMAGE_INVALID_ARGUMENT;
    }
    *out = (photoc_image){0};
    unsigned char *bytes = NULL;
    unsigned long length = 0;
    photoc_image_result result =
        read_file(path, &bytes, &length, max_file_bytes);
    if (result != PHOTOC_IMAGE_OK) {
        return result;
    }
    tjhandle handle = tjInitDecompress();
    if (handle == NULL) {
        free(bytes);
        return PHOTOC_IMAGE_CODEC_ERROR;
    }
    int width = 0;
    int height = 0;
    result = read_header(handle, bytes, length, &width, &height);
    if (result == PHOTOC_IMAGE_OK && max_dimension != 0) {
        int count = 0;
        tjscalingfactor *factors = tjGetScalingFactors(&count);
        if (factors == NULL || count <= 0) {
            result = PHOTOC_IMAGE_CODEC_ERROR;
        } else {
            uint64_t best_area = 0;
            int scaled_width = 0;
            int scaled_height = 0;
            for (int i = 0; i < count; ++i) {
                if (factors[i].num <= 0 || factors[i].denom <= 0 ||
                    factors[i].num > factors[i].denom) {
                    continue;
                }
                uint64_t candidate_width =
                    ((uint64_t)width * (uint64_t)factors[i].num +
                     (uint64_t)factors[i].denom - 1) /
                    (uint64_t)factors[i].denom;
                uint64_t candidate_height =
                    ((uint64_t)height * (uint64_t)factors[i].num +
                     (uint64_t)factors[i].denom - 1) /
                    (uint64_t)factors[i].denom;
                if (candidate_width > max_dimension ||
                    candidate_height > max_dimension ||
                    candidate_width > INT_MAX / 3 ||
                    candidate_height > INT_MAX ||
                    candidate_width * candidate_height > 4000000u) {
                    continue;
                }
                uint64_t area = candidate_width * candidate_height;
                if (area > best_area) {
                    best_area = area;
                    scaled_width = (int)candidate_width;
                    scaled_height = (int)candidate_height;
                }
            }
            if (best_area == 0) {
                result = PHOTOC_IMAGE_TOO_LARGE;
            } else {
                width = scaled_width;
                height = scaled_height;
            }
        }
    }
    size_t stride = 0;
    size_t pixel_bytes = 0;
    if (result == PHOTOC_IMAGE_OK) {
        if (width > INT_MAX / 3 ||
            (size_t)height > SIZE_MAX / ((size_t)width * 3)) {
            result = PHOTOC_IMAGE_TOO_LARGE;
        } else {
            stride = (size_t)width * 3;
            pixel_bytes = stride * (size_t)height;
        }
    }
    unsigned char *pixels = NULL;
    if (result == PHOTOC_IMAGE_OK) {
        pixels = malloc(pixel_bytes);
        if (pixels == NULL) {
            result = PHOTOC_IMAGE_NO_MEMORY;
        }
    }
    if (result == PHOTOC_IMAGE_OK &&
        tjDecompress2(handle, bytes, length, pixels, width, (int)stride, height,
                      TJPF_RGB, 0) != 0) {
        result = PHOTOC_IMAGE_INVALID_JPEG;
    }
    tjDestroy(handle);
    free(bytes);
    if (result != PHOTOC_IMAGE_OK) {
        free(pixels);
        return result;
    }
    *out = (photoc_image){(uint32_t)width, (uint32_t)height, stride,
                          pixel_bytes, pixels};
    return PHOTOC_IMAGE_OK;
}

photoc_image_result photoc_image_decode_jpeg(const char *path,
                                             photoc_image *out)
{
    return decode_jpeg(path, 0, 0, out);
}

photoc_image_result photoc_image_decode_jpeg_scaled(const char *path,
                                                    uint32_t max_dimension,
                                                    photoc_image *out)
{
    if (max_dimension == 0) {
        return PHOTOC_IMAGE_INVALID_ARGUMENT;
    }
    return decode_jpeg(path, max_dimension, 0, out);
}

photoc_image_result photoc_image_decode_jpeg_scaled_bounded(
    const char *path, uint32_t max_dimension, uint64_t max_file_bytes,
    photoc_image *out)
{
    if (max_dimension == 0 || max_file_bytes == 0)
        return PHOTOC_IMAGE_INVALID_ARGUMENT;
    return decode_jpeg(path, max_dimension, max_file_bytes, out);
}

static photoc_image_result encode_jpeg(const photoc_image *image, int quality,
                                       photoc_jpeg_buffer *out, int subsampling)
{
    if (image == NULL || out == NULL || quality < 1 || quality > 100 ||
        image->pixels == NULL || image->width == 0 || image->height == 0 ||
        image->width > INT_MAX / 3 || image->height > INT_MAX) {
        return PHOTOC_IMAGE_INVALID_ARGUMENT;
    }
    size_t row_bytes = (size_t)image->width * 3;
    if (image->stride < row_bytes || image->stride > INT_MAX ||
        (size_t)image->height > SIZE_MAX / image->stride ||
        image->pixel_bytes < image->stride * (size_t)image->height) {
        return PHOTOC_IMAGE_INVALID_ARGUMENT;
    }
    *out = (photoc_jpeg_buffer){0};
    tjhandle handle = tjInitCompress();
    if (handle == NULL) {
        return PHOTOC_IMAGE_CODEC_ERROR;
    }
    unsigned char *jpeg = NULL;
    unsigned long length = 0;
    int encoded = tjCompress2(handle, image->pixels, (int)image->width,
                              (int)image->stride, (int)image->height, TJPF_RGB,
                              &jpeg, &length, subsampling, quality, 0);
    tjDestroy(handle);
    if (encoded != 0 || jpeg == NULL || length == 0) {
        tjFree(jpeg);
        return PHOTOC_IMAGE_CODEC_ERROR;
    }
    out->data = jpeg;
    out->size = (size_t)length;
    return PHOTOC_IMAGE_OK;
}

photoc_image_result photoc_image_encode_jpeg(const photoc_image *image,
                                             int quality,
                                             photoc_jpeg_buffer *out)
{
    return encode_jpeg(image, quality, out, TJSAMP_420);
}

photoc_image_result
photoc_image_encode_jpeg_grayscale(const photoc_image *image, int quality,
                                   photoc_jpeg_buffer *out)
{
    return encode_jpeg(image, quality, out, TJSAMP_GRAY);
}

const char *photoc_image_result_message(photoc_image_result result)
{
    switch (result) {
    case PHOTOC_IMAGE_OK:
        return "image processed";
    case PHOTOC_IMAGE_INVALID_ARGUMENT:
        return "invalid image argument";
    case PHOTOC_IMAGE_IO_ERROR:
        return "image I/O error";
    case PHOTOC_IMAGE_INVALID_JPEG:
        return "invalid or unsupported JPEG";
    case PHOTOC_IMAGE_TOO_LARGE:
        return "image is too large";
    case PHOTOC_IMAGE_NO_MEMORY:
        return "out of memory";
    case PHOTOC_IMAGE_CODEC_ERROR:
        return "JPEG codec error";
    default:
        return "unknown image error";
    }
}
