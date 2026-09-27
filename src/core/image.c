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
                                     unsigned long *length)
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
                                       unsigned long length,
                                       int *width, int *height)
{
    int subsampling;
    int colorspace;
    if (tjDecompressHeader3(handle, bytes, length, width, height,
                            &subsampling, &colorspace) != 0 ||
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
    photoc_image_result result = read_file(path, &bytes, &length);
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

photoc_image_result photoc_image_decode_jpeg(const char *path,
                                              photoc_image *out)
{
    if (path == NULL || path[0] == '\0' || out == NULL) {
        return PHOTOC_IMAGE_INVALID_ARGUMENT;
    }
    *out = (photoc_image){0};
    unsigned char *bytes = NULL;
    unsigned long length = 0;
    photoc_image_result result = read_file(path, &bytes, &length);
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
        tjDecompress2(handle, bytes, length, pixels, width, (int)stride,
                      height, TJPF_RGB, 0) != 0) {
        result = PHOTOC_IMAGE_INVALID_JPEG;
    }
    tjDestroy(handle);
    free(bytes);
    if (result != PHOTOC_IMAGE_OK) {
        free(pixels);
        return result;
    }
    *out = (photoc_image){(uint32_t)width, (uint32_t)height,
                          stride, pixel_bytes, pixels};
    return PHOTOC_IMAGE_OK;
}

photoc_image_result photoc_image_encode_jpeg(const photoc_image *image,
                                              int quality,
                                              photoc_jpeg_buffer *out)
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
                              (int)image->stride, (int)image->height,
                              TJPF_RGB, &jpeg, &length, TJSAMP_420,
                              quality, 0);
    tjDestroy(handle);
    if (encoded != 0 || jpeg == NULL || length == 0) {
        tjFree(jpeg);
        return PHOTOC_IMAGE_CODEC_ERROR;
    }
    out->data = jpeg;
    out->size = (size_t)length;
    return PHOTOC_IMAGE_OK;
}

const char *photoc_image_result_message(photoc_image_result result)
{
    switch (result) {
    case PHOTOC_IMAGE_OK: return "image processed";
    case PHOTOC_IMAGE_INVALID_ARGUMENT: return "invalid image argument";
    case PHOTOC_IMAGE_IO_ERROR: return "image I/O error";
    case PHOTOC_IMAGE_INVALID_JPEG: return "invalid or unsupported JPEG";
    case PHOTOC_IMAGE_TOO_LARGE: return "image is too large";
    case PHOTOC_IMAGE_NO_MEMORY: return "out of memory";
    case PHOTOC_IMAGE_CODEC_ERROR: return "JPEG codec error";
    default: return "unknown image error";
    }
}
