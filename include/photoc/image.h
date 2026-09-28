#ifndef PHOTOC_IMAGE_H
#define PHOTOC_IMAGE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    PHOTOC_IMAGE_OK = 0,
    PHOTOC_IMAGE_INVALID_ARGUMENT,
    PHOTOC_IMAGE_IO_ERROR,
    PHOTOC_IMAGE_INVALID_JPEG,
    PHOTOC_IMAGE_TOO_LARGE,
    PHOTOC_IMAGE_NO_MEMORY,
    PHOTOC_IMAGE_CODEC_ERROR
} photoc_image_result;

/* Decoded, top-to-bottom RGB pixels (three bytes per pixel). pixels is owned
   by the struct and released with photoc_image_cleanup. The stride and
   pixel_bytes fields allow callers and the encoder to check buffer bounds. */
typedef struct {
    uint32_t width;
    uint32_t height;
    size_t stride;
    size_t pixel_bytes;
    unsigned char *pixels;
} photoc_image;

/* TurboJPEG-allocated JPEG bytes, owned by the struct. Release with
   photoc_jpeg_buffer_cleanup, not free(). No EXIF/ICC is copied into output. */
typedef struct {
    unsigned char *data;
    size_t size;
} photoc_jpeg_buffer;

/* Read raw JPEG dimensions; EXIF orientation is not applied. On error,
   width and height are unchanged. I/O errors preserve errno. */
photoc_image_result photoc_image_jpeg_dimensions(const char *path,
                                                 uint32_t *width,
                                                 uint32_t *height);

/* Decode a JPEG file to RGB. out must be zero-initialized or cleaned first.
   On success out owns pixels; on error out remains empty. */
photoc_image_result photoc_image_decode_jpeg(const char *path,
                                             photoc_image *out);

/* Decode at the largest TurboJPEG-supported scale whose width and height are
   at most max_dimension. The decoded buffer is capped at 4 million pixels.
   This avoids full-size RGB allocation for large JPEGs; compressed JPEG bytes
   are still read into memory. Returns TOO_LARGE if no supported scale fits.
   out follows the same ownership and cleanup rules as decode_jpeg. */
photoc_image_result photoc_image_decode_jpeg_scaled(const char *path,
                                                    uint32_t max_dimension,
                                                    photoc_image *out);

/* Encode RGB pixels as JPEG. quality is an integer from 1 to 100. out must
   be zero-initialized or cleaned first. On success out owns data; on error
   out remains empty. This function never writes a file. */
photoc_image_result photoc_image_encode_jpeg(const photoc_image *image,
                                             int quality,
                                             photoc_jpeg_buffer *out);

void photoc_image_cleanup(photoc_image *image);
void photoc_jpeg_buffer_cleanup(photoc_jpeg_buffer *buffer);

/* Static diagnostic text; never free it. */
const char *photoc_image_result_message(photoc_image_result result);

#endif
