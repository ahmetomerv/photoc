#include "photoc/sharpness.h"

#include <stdio.h>

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static void test_ordering(uint32_t max_dimension)
{
    double sharp = -1.0;
    double blurred = -1.0;
    double flat = -1.0;
    CHECK(photoc_sharpness_score_jpeg(PHOTOC_SHARPNESS_FIXTURES "/sharp.jpg",
                                      max_dimension,
                                      &sharp) == PHOTOC_IMAGE_OK);
    CHECK(photoc_sharpness_score_jpeg(PHOTOC_SHARPNESS_FIXTURES "/blurred.jpg",
                                      max_dimension,
                                      &blurred) == PHOTOC_IMAGE_OK);
    CHECK(photoc_sharpness_score_jpeg(PHOTOC_SHARPNESS_FIXTURES "/flat.jpg",
                                      max_dimension, &flat) == PHOTOC_IMAGE_OK);
    if (!(sharp > blurred && blurred > flat && flat >= 0.0)) {
        fprintf(stderr,
                "Unexpected score order at %u pixels: sharp=%.3f blurred=%.3f "
                "flat=%.3f\n",
                max_dimension, sharp, blurred, flat);
        ++failures;
    }
    double repeated = -1.0;
    CHECK(photoc_sharpness_score_jpeg(PHOTOC_SHARPNESS_FIXTURES "/sharp.jpg",
                                      max_dimension,
                                      &repeated) == PHOTOC_IMAGE_OK);
    CHECK(repeated == sharp);
}

static void test_scaled_decode(void)
{
    const char *large = PHOTOC_SHARPNESS_FIXTURES "/large_sharp.jpg";
    photoc_image image = {0};
    CHECK(photoc_image_decode_jpeg_scaled(large, 1024, &image) ==
          PHOTOC_IMAGE_OK);
    CHECK(image.width <= 1024 && image.height <= 1024);
    CHECK(image.width < 2048 && image.height < 2048);
    CHECK(image.pixel_bytes <= 1024u * 1024u * 3u);
    photoc_image_cleanup(&image);

    CHECK(photoc_image_decode_jpeg_scaled(large, 2048, &image) ==
          PHOTOC_IMAGE_OK);
    CHECK((size_t)image.width * image.height <= 4000000u);
    CHECK(image.width < 2048 && image.height < 2048);
    photoc_image_cleanup(&image);

    CHECK(photoc_image_decode_jpeg_scaled(large, 1, &image) ==
          PHOTOC_IMAGE_TOO_LARGE);
    CHECK(image.pixels == NULL);
    CHECK(photoc_image_decode_jpeg_scaled(large, 0, &image) ==
          PHOTOC_IMAGE_INVALID_ARGUMENT);
    CHECK(image.pixels == NULL);

    CHECK(photoc_image_decode_jpeg(large, &image) == PHOTOC_IMAGE_OK);
    CHECK(image.width == 2048 && image.height == 2048);
    photoc_image_cleanup(&image);
}

static void test_errors(void)
{
    double score = -17.0;
    CHECK(photoc_sharpness_score_jpeg(NULL, 1024, &score) ==
          PHOTOC_IMAGE_INVALID_ARGUMENT);
    CHECK(photoc_sharpness_score_jpeg("", 1024, &score) ==
          PHOTOC_IMAGE_INVALID_ARGUMENT);
    CHECK(photoc_sharpness_score_jpeg(PHOTOC_SHARPNESS_FIXTURES "/sharp.jpg", 0,
                                      &score) == PHOTOC_IMAGE_INVALID_ARGUMENT);
    CHECK(photoc_sharpness_score_jpeg(PHOTOC_SHARPNESS_FIXTURES "/sharp.jpg",
                                      1024,
                                      NULL) == PHOTOC_IMAGE_INVALID_ARGUMENT);
    CHECK(photoc_sharpness_score_jpeg(PHOTOC_SHARPNESS_FIXTURES "/invalid.jpg",
                                      1024,
                                      &score) == PHOTOC_IMAGE_INVALID_JPEG);
    CHECK(photoc_sharpness_score_jpeg(
              PHOTOC_SHARPNESS_FIXTURES "/unsupported.png", 1024, &score) ==
          PHOTOC_IMAGE_INVALID_JPEG);
    CHECK(photoc_sharpness_score_jpeg(PHOTOC_SHARPNESS_FIXTURES "/missing.jpg",
                                      1024, &score) == PHOTOC_IMAGE_IO_ERROR);
    CHECK(score == -17.0);
}

int main(void)
{
    test_ordering(256);
    test_ordering(128);
    test_scaled_decode();
    test_errors();
    if (failures != 0) {
        fprintf(stderr, "%d sharpness test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
