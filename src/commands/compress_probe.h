#ifndef PHOTOC_COMPRESS_PROBE_H
#define PHOTOC_COMPRESS_PROBE_H

#include "photoc/image.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const photoc_image *image; /* Borrowed for the synchronous search. */
    uint64_t metadata_overhead;
    uint64_t target_bytes;
    photoc_image_result error;
    bool grayscale;
    int retained_quality;
    photoc_jpeg_buffer retained; /* Owned until taken or cleared. */
    size_t peak_probe_buffers; /* Includes the candidate before it is freed. */
} photoc_compress_probe;

int photoc_compress_probe_size(int quality, uint64_t *size, void *user_data);

/* Transfers the chosen JPEG to an empty out; caller then owns and cleans it. */
bool photoc_compress_probe_take(photoc_compress_probe *probe, int quality,
                                photoc_jpeg_buffer *out);

/* Releases any retained JPEG, including after a failed quality search. */
void photoc_compress_probe_clear(photoc_compress_probe *probe);

#endif
