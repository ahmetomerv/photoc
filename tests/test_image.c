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

static void test_orientations(void)
{
    static const char *expected[] = {"ABCDEF", "BADCFE", "FEDCBA", "EFCDAB",
                                     "ACEBDF", "ECAFDB", "FDBECA", "BDFACE"};
    for (uint16_t orientation = 1; orientation <= 8; ++orientation) {
        photoc_image image = {0};
        image.width = 2;
        image.height = 3;
        image.stride = 6;
        image.pixel_bytes = 18;
        image.pixels = malloc(18);
        CHECK(image.pixels != NULL);
        if (image.pixels == NULL)
            return;
        for (size_t i = 0; i < 6; ++i)
            memset(image.pixels + i * 3, (int)('A' + i), 3);
        CHECK(photoc_image_apply_orientation(&image, orientation) ==
              PHOTOC_IMAGE_OK);
        CHECK(image.width == (orientation >= 5 ? 3u : 2u));
        CHECK(image.height == (orientation >= 5 ? 2u : 3u));
        for (size_t i = 0; i < 6; ++i)
            CHECK(image.pixels[i * 3] ==
                  (unsigned char)expected[orientation - 1][i]);
        photoc_image_cleanup(&image);
    }
}

int main(void)
{
    test_orientations();
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

    /* The single-read path must decode exactly like the file path. */
    unsigned char *bytes = NULL;
    size_t length = 0;
    CHECK(photoc_image_read_file(source, 0, &bytes, &length) ==
          PHOTOC_IMAGE_OK);
    CHECK(bytes != NULL && length > 0);
    photoc_image from_buffer = {0};
    CHECK(photoc_image_decode_jpeg_buffer(bytes, length, &from_buffer) ==
          PHOTOC_IMAGE_OK);
    CHECK(from_buffer.width == image.width &&
          from_buffer.height == image.height);
    CHECK(from_buffer.stride == image.stride &&
          from_buffer.pixel_bytes == image.pixel_bytes);
    CHECK(from_buffer.pixels != NULL && image.pixels != NULL);
    if (from_buffer.pixels != NULL && image.pixels != NULL) {
        CHECK(memcmp(from_buffer.pixels, image.pixels, image.pixel_bytes) == 0);
    }
    photoc_image_cleanup(&from_buffer);

    /* A reusable decode context must match the plain decoders and stay usable
       after a failed decode, so a batch can share one handle. */
    photoc_image_decoder *decoder = photoc_image_decoder_create();
    CHECK(decoder != NULL);
    photoc_image context_image = {0};
    CHECK(photoc_image_decode_jpeg_with(decoder, source, &context_image) ==
          PHOTOC_IMAGE_OK);
    CHECK(context_image.width == image.width &&
          context_image.height == image.height);
    CHECK(context_image.pixels != NULL && image.pixels != NULL);
    if (context_image.pixels != NULL && image.pixels != NULL) {
        CHECK(memcmp(context_image.pixels, image.pixels, image.pixel_bytes) ==
              0);
    }
    photoc_image context_rejected = {0};
    CHECK(photoc_image_decode_jpeg_scaled_bounded_with(decoder, source, 4096, 1,
                                                       &context_rejected) ==
          PHOTOC_IMAGE_TOO_LARGE);
    CHECK(context_rejected.pixels == NULL);
    photoc_image context_scaled = {0};
    CHECK(photoc_image_decode_jpeg_scaled_bounded_with(
              decoder, source, 4096, 67108864, &context_scaled) ==
          PHOTOC_IMAGE_OK);
    CHECK(context_scaled.width == image.width &&
          context_scaled.height == image.height);
    photoc_image context_buffer = {0};
    CHECK(photoc_image_decode_jpeg_buffer_with(
              decoder, bytes, length, &context_buffer) == PHOTOC_IMAGE_OK);
    CHECK(context_buffer.pixels != NULL);
    photoc_image context_failed = {0};
    CHECK(photoc_image_decode_jpeg_with(decoder, invalid, &context_failed) ==
          PHOTOC_IMAGE_INVALID_JPEG);
    CHECK(context_failed.pixels == NULL);
    photoc_image context_after = {0};
    CHECK(photoc_image_decode_jpeg_with(decoder, source, &context_after) ==
          PHOTOC_IMAGE_OK);
    /* A NULL context falls back to a temporary handle. */
    photoc_image context_null = {0};
    CHECK(photoc_image_decode_jpeg_with(NULL, source, &context_null) ==
          PHOTOC_IMAGE_OK);
    photoc_image_cleanup(&context_null);
    photoc_image_cleanup(&context_after);
    photoc_image_cleanup(&context_buffer);
    photoc_image_cleanup(&context_scaled);
    photoc_image_cleanup(&context_image);
    photoc_image_decoder_destroy(decoder);
    photoc_image_decoder_destroy(NULL);
    free(bytes);

    photoc_image rejected = {0};
    CHECK(photoc_image_decode_jpeg_buffer(NULL, 1, &rejected) ==
          PHOTOC_IMAGE_INVALID_ARGUMENT);
    CHECK(photoc_image_decode_jpeg_buffer((const unsigned char *)"", 0,
                                          &rejected) ==
          PHOTOC_IMAGE_INVALID_JPEG);
    CHECK(rejected.pixels == NULL);
    CHECK(photoc_image_read_file(source, 0, NULL, &length) ==
          PHOTOC_IMAGE_INVALID_ARGUMENT);
    unsigned char *no_bytes = NULL;
    CHECK(photoc_image_read_file("/nonexistent/photoc.jpg", 0, &no_bytes,
                                 &length) == PHOTOC_IMAGE_IO_ERROR);
    CHECK(no_bytes == NULL && length == 0);

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
