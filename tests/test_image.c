#define _POSIX_C_SOURCE 200809L

#include "photoc/image.h"
#include "photoc/fs.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s (errno=%d)\n", __FILE__, __LINE__,      \
                    #condition, errno);                                        \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static void write_jpeg(const char *path, const photoc_jpeg_buffer *jpeg)
{
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file == NULL)
        return;
    CHECK(fwrite(jpeg->data, 1, jpeg->size, file) == jpeg->size);
    CHECK(fclose(file) == 0);
}

int main(void)
{
    const char *source = PHOTOC_IMAGE_FIXTURES "/with_exif.jpg";
    const char *invalid = PHOTOC_IMAGE_FIXTURES "/invalid.jpg";
    const char *unsupported = PHOTOC_IMAGE_FIXTURES "/unsupported.png";
    uint32_t width = 0;
    uint32_t height = 0;
    CHECK(photoc_image_jpeg_dimensions(source, &width, &height) ==
          PHOTOC_IMAGE_OK);
    CHECK(width == 3 && height == 2);

    photoc_image image = {0};
    CHECK(photoc_image_decode_jpeg(source, &image) == PHOTOC_IMAGE_OK);
    photoc_image bounded = {0};
    CHECK(photoc_image_decode_jpeg_scaled_bounded(source, 4096, 1, &bounded) ==
          PHOTOC_IMAGE_TOO_LARGE);
    CHECK(bounded.pixels == NULL);
    CHECK(photoc_image_decode_jpeg_scaled_bounded(source, 4096, 67108864,
                                                  &bounded) == PHOTOC_IMAGE_OK);
    photoc_image_cleanup(&bounded);
    CHECK(image.width == 3 && image.height == 2);
    CHECK(image.stride == 9 && image.pixel_bytes == 18);
    CHECK(image.pixels != NULL);

    photoc_jpeg_buffer encoded = {0};
    CHECK(photoc_image_encode_jpeg(&image, 85, &encoded) == PHOTOC_IMAGE_OK);
    CHECK(encoded.data != NULL && encoded.size > 4);
    if (encoded.data != NULL && encoded.size > 4) {
        CHECK(encoded.data[0] == 0xff && encoded.data[1] == 0xd8);
        CHECK(encoded.data[encoded.size - 2] == 0xff &&
              encoded.data[encoded.size - 1] == 0xd9);
    }

    const char *temporary = getenv("TMPDIR");
    if (temporary == NULL || temporary[0] == '\0')
        temporary = "/tmp";
    char *output = NULL;
    CHECK(photoc_fs_join(temporary, "photoc-image-roundtrip-XXXXXX", &output) ==
          0);
    int descriptor = output == NULL ? -1 : mkstemp(output);
    CHECK(descriptor >= 0);
    if (descriptor >= 0) {
        CHECK(close(descriptor) == 0);
        if (encoded.data != NULL) {
            write_jpeg(output, &encoded);
            width = 0;
            height = 0;
            CHECK(photoc_image_jpeg_dimensions(output, &width, &height) ==
                  PHOTOC_IMAGE_OK);
            CHECK(width == 3 && height == 2);
            photoc_image roundtrip = {0};
            CHECK(photoc_image_decode_jpeg(output, &roundtrip) ==
                  PHOTOC_IMAGE_OK);
            CHECK(roundtrip.width == 3 && roundtrip.height == 2);
            CHECK(roundtrip.pixels != NULL && roundtrip.pixel_bytes == 18);
            photoc_image_cleanup(&roundtrip);
        }
        CHECK(unlink(output) == 0);
    }
    free(output);

    photoc_jpeg_buffer low_quality = {0};
    CHECK(photoc_image_encode_jpeg(&image, 20, &low_quality) ==
          PHOTOC_IMAGE_OK);
    CHECK(low_quality.data != NULL && low_quality.size > 4);
    if (low_quality.data != NULL && encoded.data != NULL) {
        CHECK(low_quality.size != encoded.size ||
              memcmp(low_quality.data, encoded.data, encoded.size) != 0);
    }
    photoc_jpeg_buffer_cleanup(&low_quality);
    CHECK(low_quality.data == NULL && low_quality.size == 0);
    CHECK(photoc_image_encode_jpeg(&image, 0, &low_quality) ==
          PHOTOC_IMAGE_INVALID_ARGUMENT);
    CHECK(photoc_image_encode_jpeg(&image, 101, &low_quality) ==
          PHOTOC_IMAGE_INVALID_ARGUMENT);
    photoc_image bad_stride = image;
    bad_stride.stride = 1;
    CHECK(photoc_image_encode_jpeg(&bad_stride, 85, &low_quality) ==
          PHOTOC_IMAGE_INVALID_ARGUMENT);

    width = 123;
    height = 456;
    CHECK(photoc_image_jpeg_dimensions(invalid, &width, &height) ==
          PHOTOC_IMAGE_INVALID_JPEG);
    CHECK(width == 123 && height == 456);
    CHECK(photoc_image_jpeg_dimensions(unsupported, &width, &height) ==
          PHOTOC_IMAGE_INVALID_JPEG);
    CHECK(photoc_image_jpeg_dimensions("/nonexistent/photoc.jpg", &width,
                                       &height) == PHOTOC_IMAGE_IO_ERROR);
    photoc_image failed_decode = {0};
    CHECK(photoc_image_decode_jpeg(invalid, &failed_decode) ==
          PHOTOC_IMAGE_INVALID_JPEG);
    CHECK(failed_decode.pixels == NULL);

    photoc_jpeg_buffer_cleanup(&encoded);
    photoc_image_cleanup(&image);
    CHECK(image.pixels == NULL && image.width == 0 && image.height == 0);
    photoc_image_cleanup(&image);
    photoc_jpeg_buffer_cleanup(&encoded);
    if (failures != 0) {
        fprintf(stderr, "%d image test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
