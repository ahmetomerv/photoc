#include "photoc/photo_image.h"

#include "photoc/arw.h"
#include "photoc/fs.h"

#include <stdlib.h>

photoc_image_result photoc_photo_decode_scaled_bounded_with(
    photoc_image_decoder *decoder, const char *path, uint32_t max_dimension,
    uint64_t max_source_bytes, photoc_image *out)
{
    if (path == NULL || out == NULL || max_dimension == 0 ||
        max_source_bytes == 0) {
        return PHOTOC_IMAGE_INVALID_ARGUMENT;
    }
    if (!photoc_fs_is_arw(path)) {
        return photoc_image_decode_jpeg_scaled_bounded_with(
            decoder, path, max_dimension, max_source_bytes, out);
    }
    unsigned char *preview = NULL;
    size_t length = 0;
    photoc_image_result result =
        photoc_arw_read_preview(path, max_source_bytes, &preview, &length);
    if (result == PHOTOC_IMAGE_OK) {
        result = photoc_image_decode_jpeg_buffer_scaled_with(
            decoder, preview, length, max_dimension, out);
        free(preview);
    }
    return result;
}
