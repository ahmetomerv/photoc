#include "photoc/image_analysis.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static void test_grayscale(void)
{
    unsigned char rgb[] = {0, 0, 0,   255, 255, 255, 255, 0,
                           0, 0, 255, 0,   0,   0,   255};
    photoc_image input = {5, 1, 15, sizeof(rgb), rgb};
    photoc_gray_image gray = {0};
    CHECK(photoc_gray_from_rgb(&input, 1, &gray) == PHOTOC_ANALYSIS_OK);
    CHECK(gray.width == 5 && gray.height == 1 && gray.pixel_count == 5);
    const uint8_t expected[] = {0, 255, 76, 150, 29};
    CHECK(gray.pixels != NULL && memcmp(gray.pixels, expected, 5) == 0);
    CHECK(photoc_gray_from_rgb(&input, 1, &gray) ==
          PHOTOC_ANALYSIS_INVALID_ARGUMENT);
    CHECK(gray.pixels != NULL && memcmp(gray.pixels, expected, 5) == 0);
    photoc_gray_cleanup(&gray);
    CHECK(gray.pixels == NULL && gray.pixel_count == 0);
    photoc_gray_cleanup(&gray);
}

static void test_sampling(void)
{
    unsigned char rgb[3 * 16];
    memset(rgb, 0xee, sizeof(rgb));
    for (size_t y = 0; y < 3; ++y) {
        for (size_t x = 0; x < 4; ++x) {
            uint8_t value = (uint8_t)(y * 10 + x);
            size_t index = y * 16 + x * 3;
            rgb[index] = value;
            rgb[index + 1] = value;
            rgb[index + 2] = value;
        }
    }
    photoc_image input = {4, 3, 16, sizeof(rgb), rgb};
    photoc_gray_image gray = {0};
    CHECK(photoc_gray_from_rgb(&input, 2, &gray) == PHOTOC_ANALYSIS_OK);
    CHECK(gray.width == 2 && gray.height == 2 && gray.pixel_count == 4);
    const uint8_t expected[] = {0, 2, 20, 22};
    CHECK(gray.pixels != NULL && memcmp(gray.pixels, expected, 4) == 0);
    photoc_gray_cleanup(&gray);

    CHECK(photoc_gray_from_rgb(&input, 4, &gray) == PHOTOC_ANALYSIS_OK);
    CHECK(gray.width == 1 && gray.height == 1 && gray.pixels[0] == 0);
    photoc_gray_cleanup(&gray);

    CHECK(photoc_gray_from_rgb(&input, 0, &gray) ==
          PHOTOC_ANALYSIS_INVALID_ARGUMENT);
    input.stride = 11;
    CHECK(photoc_gray_from_rgb(&input, 1, &gray) ==
          PHOTOC_ANALYSIS_INVALID_ARGUMENT);
    input.stride = 16;
    input.pixel_bytes = 47;
    CHECK(photoc_gray_from_rgb(&input, 1, &gray) ==
          PHOTOC_ANALYSIS_INVALID_ARGUMENT);
    CHECK(gray.pixels == NULL);

    uint8_t dummy = 0;
    photoc_image large = {5000, 5000, 15000, 75000000, &dummy};
    CHECK(photoc_gray_from_rgb(&large, 1, &gray) == PHOTOC_ANALYSIS_TOO_LARGE);
    CHECK(gray.pixels == NULL);
}

static void test_laplacian(void)
{
    photoc_gray_image gray = {5, 5, 25, calloc(25, 1)};
    CHECK(gray.pixels != NULL);
    if (gray.pixels == NULL)
        return;
    gray.pixels[12] = 255;
    photoc_laplacian_image lap = {0};
    CHECK(photoc_gray_laplacian(&gray, &lap) == PHOTOC_ANALYSIS_OK);
    CHECK(lap.width == 5 && lap.height == 5 && lap.value_count == 25);
    if (lap.values != NULL) {
        for (size_t i = 0; i < lap.value_count; ++i) {
            int16_t expected = 0;
            if (i == 12)
                expected = 1020;
            if (i == 7 || i == 11 || i == 13 || i == 17)
                expected = -255;
            CHECK(lap.values[i] == expected);
        }
        double variance = -1.0;
        CHECK(photoc_variance_i16(lap.values, lap.value_count, &variance) ==
              PHOTOC_ANALYSIS_OK);
        CHECK(variance > 52019.999 && variance < 52020.001);
    }
    CHECK(photoc_gray_laplacian(&gray, &lap) ==
          PHOTOC_ANALYSIS_INVALID_ARGUMENT);
    photoc_laplacian_cleanup(&lap);
    CHECK(lap.values == NULL && lap.value_count == 0);
    photoc_laplacian_cleanup(&lap);

    memset(gray.pixels, 100, gray.pixel_count);
    CHECK(photoc_gray_laplacian(&gray, &lap) == PHOTOC_ANALYSIS_OK);
    for (size_t i = 0; i < lap.value_count; ++i) {
        CHECK(lap.values[i] == 0);
    }
    photoc_laplacian_cleanup(&lap);
    photoc_gray_cleanup(&gray);

    uint8_t tiny_pixels[] = {1, 2, 3, 4};
    photoc_gray_image tiny = {2, 2, 4, tiny_pixels};
    CHECK(photoc_gray_laplacian(&tiny, &lap) == PHOTOC_ANALYSIS_OK);
    for (size_t i = 0; i < lap.value_count; ++i) {
        CHECK(lap.values[i] == 0);
    }
    photoc_laplacian_cleanup(&lap);

    photoc_gray_image malformed = {2, 2, 3, tiny_pixels};
    CHECK(photoc_gray_laplacian(&malformed, &lap) ==
          PHOTOC_ANALYSIS_INVALID_ARGUMENT);
    photoc_gray_image large = {5000, 5000, 25000000, tiny_pixels};
    CHECK(photoc_gray_laplacian(&large, &lap) == PHOTOC_ANALYSIS_TOO_LARGE);
    CHECK(lap.values == NULL);
}

static void test_variance(void)
{
    int16_t values[] = {1, 2, 3, 4};
    double result = -1.0;
    CHECK(photoc_variance_i16(values, 4, &result) == PHOTOC_ANALYSIS_OK);
    CHECK(result == 1.25);
    int16_t negative[] = {-2, 0, 2};
    CHECK(photoc_variance_i16(negative, 3, &result) == PHOTOC_ANALYSIS_OK);
    CHECK(result > 2.6666 && result < 2.6667);
    CHECK(photoc_variance_i16(values, 1, &result) == PHOTOC_ANALYSIS_OK);
    CHECK(result == 0.0);
    CHECK(photoc_variance_i16(values, 0, &result) ==
          PHOTOC_ANALYSIS_INVALID_ARGUMENT);
    CHECK(result == 0.0);
    CHECK(photoc_variance_i16(NULL, 4, &result) ==
          PHOTOC_ANALYSIS_INVALID_ARGUMENT);
    CHECK(photoc_variance_i16(values, 4, NULL) ==
          PHOTOC_ANALYSIS_INVALID_ARGUMENT);
}

int main(void)
{
    test_grayscale();
    test_sampling();
    test_laplacian();
    test_variance();
    CHECK(strcmp(photoc_analysis_result_message(PHOTOC_ANALYSIS_TOO_LARGE),
                 "sampled image is too large") == 0);
    if (failures != 0) {
        fprintf(stderr, "%d image analysis test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
