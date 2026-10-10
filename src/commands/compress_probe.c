#include "compress_probe.h"

#include <errno.h>
#include <stdint.h>

int photoc_compress_probe_size(int quality, uint64_t *size, void *user_data)
{
    photoc_compress_probe *probe = user_data;
    if (probe->retained.data != NULL && quality == probe->retained_quality) {
        *size = (uint64_t)probe->retained.size + probe->metadata_overhead;
        return 0;
    }

    photoc_jpeg_buffer encoded = {0};
    probe->error =
        probe->grayscale
            ? photoc_image_encode_jpeg_grayscale(probe->image, quality,
                                                 &encoded)
            : photoc_image_encode_jpeg(probe->image, quality, &encoded);
    if (probe->error != PHOTOC_IMAGE_OK) {
        photoc_jpeg_buffer_cleanup(&encoded);
        errno = EIO;
        return -1;
    }
    if (encoded.size > UINT64_MAX - probe->metadata_overhead) {
        photoc_jpeg_buffer_cleanup(&encoded);
        errno = EOVERFLOW;
        return -1;
    }

    /* A candidate and the previous winner can coexist until this decision.
       Between probes only the winner is retained. */
    size_t live_buffers = probe->retained.data == NULL ? 1 : 2;
    if (live_buffers > probe->peak_probe_buffers) {
        probe->peak_probe_buffers = live_buffers;
    }
    *size = (uint64_t)encoded.size + probe->metadata_overhead;
    if (probe->retained.data == NULL ||
        (*size <= probe->target_bytes && quality > probe->retained_quality)) {
        photoc_jpeg_buffer_cleanup(&probe->retained);
        probe->retained = encoded;
        probe->retained_quality = quality;
    } else {
        photoc_jpeg_buffer_cleanup(&encoded);
    }
    return 0;
}

bool photoc_compress_probe_take(photoc_compress_probe *probe, int quality,
                                photoc_jpeg_buffer *out)
{
    if (probe->retained.data == NULL || probe->retained_quality != quality) {
        return false;
    }
    *out = probe->retained;
    probe->retained = (photoc_jpeg_buffer){0};
    probe->retained_quality = 0;
    return true;
}

void photoc_compress_probe_clear(photoc_compress_probe *probe)
{
    photoc_jpeg_buffer_cleanup(&probe->retained);
    probe->retained_quality = 0;
}
