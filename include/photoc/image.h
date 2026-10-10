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

/* Read an entire file into a newly allocated buffer. max_file_bytes == 0
   means no size cap. On success *bytes owns malloc'd memory that the caller
   must free() and *length is its byte count; *bytes is NULL on failure. An
   empty file returns INVALID_JPEG and an over-cap file returns TOO_LARGE.
   I/O errors preserve errno. This is the shared read used before JPEG codecs
   run, so callers can decode and inspect the same bytes once. */
photoc_image_result photoc_image_read_file(const char *path,
                                           uint64_t max_file_bytes,
                                           unsigned char **bytes,
                                           size_t *length);

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

/* As above, but refuse compressed files larger than max_file_bytes before
   allocating the compressed-byte buffer. A positive cap is required. This
   protects commands that need a strict source-file memory limit. */
photoc_image_result photoc_image_decode_jpeg_scaled_bounded(
    const char *path, uint32_t max_dimension, uint64_t max_file_bytes,
    photoc_image *out);

/* Decode an in-memory JPEG to RGB at full resolution. The caller retains
   ownership of bytes, which must remain valid for the synchronous call; no
   copy is made. out follows the same ownership and cleanup rules as
   decode_jpeg. A NULL buffer or output returns INVALID_ARGUMENT. */
photoc_image_result photoc_image_decode_jpeg_buffer(const unsigned char *bytes,
                                                    size_t length,
                                                    photoc_image *out);

/* Reusable JPEG decode context. A TurboJPEG handle is not thread-safe, so use
   one context per worker or thread; a single context may decode any number of
   files in a batch, avoiding a handle create/destroy per file. Create before
   the batch and destroy when it ends. */
typedef struct photoc_image_decoder photoc_image_decoder;

/* Allocates a context with its own decoder handle. Returns NULL with no
   allocation retained on failure. */
photoc_image_decoder *photoc_image_decoder_create(void);

/* Releases a context and its handle. NULL is accepted. */
void photoc_image_decoder_destroy(photoc_image_decoder *decoder);

/* Context-aware variants of the decode functions above. Each behaves
   identically except that a non-NULL decoder reuses its handle; a NULL
   decoder creates and releases a temporary one, matching the functions
   without the `_with` suffix. Output ownership is unchanged. */
photoc_image_result photoc_image_decode_jpeg_with(photoc_image_decoder *decoder,
                                                  const char *path,
                                                  photoc_image *out);
photoc_image_result photoc_image_decode_jpeg_scaled_bounded_with(
    photoc_image_decoder *decoder, const char *path, uint32_t max_dimension,
    uint64_t max_file_bytes, photoc_image *out);
photoc_image_result
photoc_image_decode_jpeg_buffer_with(photoc_image_decoder *decoder,
                                     const unsigned char *bytes, size_t length,
                                     photoc_image *out);

/* Encode RGB pixels as JPEG. quality is an integer from 1 to 100. out must
   be zero-initialized or cleaned first. On success out owns data; on error
   out remains empty. This function never writes a file. */
photoc_image_result photoc_image_encode_jpeg(const photoc_image *image,
                                             int quality,
                                             photoc_jpeg_buffer *out);

/* Map a displayed pixel to its source pixel for EXIF orientation 1-8.
   Coordinates must be within the oriented image. Unknown orientation values
   behave as 1. */
void photoc_image_source_coordinate(uint32_t x, uint32_t y,
                                    const photoc_image *source,
                                    uint16_t orientation, uint32_t *raw_x,
                                    uint32_t *raw_y);

/* Apply EXIF orientation to RGB pixels before metadata-free preview encoding.
   On allocation failure the original image remains unchanged. */
photoc_image_result photoc_image_apply_orientation(photoc_image *image,
                                                   uint16_t orientation);

/* Encode decoded grayscale RGB samples as a one-component JPEG. This keeps
   grayscale ICC profiles associated with grayscale image data. Ownership and
   quality rules are identical to encode_jpeg. No ICC conversion is performed. */
photoc_image_result
photoc_image_encode_jpeg_grayscale(const photoc_image *image, int quality,
                                   photoc_jpeg_buffer *out);

void photoc_image_cleanup(photoc_image *image);
void photoc_jpeg_buffer_cleanup(photoc_jpeg_buffer *buffer);

/* Static diagnostic text; never free it. */
const char *photoc_image_result_message(photoc_image_result result);

#endif
