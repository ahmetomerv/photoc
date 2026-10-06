#define _POSIX_C_SOURCE 200809L

#include "review_display.h"
#include "review_terminal.h"

#include "photoc/sharpness.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s (errno=%d)\n", __FILE__, __LINE__,      \
                    #condition, errno);                                        \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static char *contents(FILE *stream)
{
    if (fflush(stream) != 0 || fseek(stream, 0, SEEK_END) != 0)
        return NULL;
    long length = ftell(stream);
    if (length < 0 || fseek(stream, 0, SEEK_SET) != 0)
        return NULL;
    char *result = malloc((size_t)length + 1);
    if (result == NULL)
        return NULL;
    if (fread(result, 1, (size_t)length, stream) != (size_t)length) {
        free(result);
        return NULL;
    }
    result[length] = '\0';
    return result;
}

static int metadata_display(void)
{
    char make[] = "Make\t\033[31m";
    char model[] = "Model\nZ";
    char lens[] = "Lens\033";
    char timestamp[] = "2026:01:02 03:04:05";
    Photo photo = {.camera_make = make,
                   .camera_model = model,
                   .lens_model = lens,
                   .capture_timestamp = timestamp,
                   .has_width = true,
                   .width = 300,
                   .has_height = true,
                   .height = 200,
                   .has_iso = true,
                   .iso = 100,
                   .has_aperture = true,
                   .aperture = 2.8,
                   .has_exposure_time = true,
                   .exposure_time = 0.01,
                   .has_focal_length = true,
                   .focal_length = 50};
    FILE *stream = tmpfile();
    CHECK(stream != NULL);
    review_display_metadata(stream, &photo, false);
    char *compact = contents(stream);
    CHECK(compact != NULL);
    CHECK(strstr(compact, "Make\\x09\\x1B[31m Model\\x0AZ") != NULL);
    CHECK(strstr(compact, "300 x 200") != NULL);
    CHECK(strstr(compact, "1/100s  f/2.8  ISO 100  50mm") != NULL);
    CHECK(strstr(compact, "2026:01:02 03:04:05") != NULL);
    CHECK(strstr(compact, "Details") == NULL);
    CHECK(strchr(compact, '\033') == NULL);
    free(compact);
    fclose(stream);

    stream = tmpfile();
    CHECK(stream != NULL);
    review_display_metadata(stream, &photo, true);
    char *detailed = contents(stream);
    CHECK(detailed != NULL);
    CHECK(strstr(detailed, "Details\nCamera: Make\\x09\\x1B[31m") != NULL);
    CHECK(strstr(detailed, "Dimensions: 300 x 200") != NULL);
    CHECK(strstr(detailed, "Exposure: 1/100s") != NULL);
    CHECK(strstr(detailed, "Captured: 2026:01:02 03:04:05") != NULL);
    CHECK(strstr(detailed, "Lens: Lens\\x1B") != NULL);
    free(detailed);
    fclose(stream);

    stream = tmpfile();
    CHECK(stream != NULL);
    Photo sparse = {.has_width = true,
                    .width = 3,
                    .has_height = true,
                    .height = 2,
                    .has_exposure_time = true,
                    .exposure_time = 0};
    review_display_metadata(stream, &sparse, true);
    char *minimal = contents(stream);
    CHECK(minimal != NULL);
    CHECK(strstr(minimal, "Dimensions: 3 x 2") != NULL);
    CHECK(strstr(minimal, "Camera:") == NULL);
    CHECK(strstr(minimal, "Exposure:") == NULL);
    CHECK(strstr(minimal, "ISO") == NULL);
    free(minimal);
    fclose(stream);
    return 0;
}

static int sharpness_cache(void)
{
    review_score_cache success = {0};
    CHECK(!success.attempted);
    CHECK(review_score_get(&success, PHOTOC_REVIEW_FIXTURES "/flat.jpg") ==
          PHOTOC_IMAGE_OK);
    CHECK(success.attempted && success.score == 0.0);
    CHECK(review_score_get(&success, PHOTOC_REVIEW_FIXTURES "/sharp.jpg") ==
          PHOTOC_IMAGE_OK);
    CHECK(success.score == 0.0);

    review_score_cache failure = {0};
    CHECK(review_score_get(&failure, PHOTOC_REVIEW_FIXTURES "/missing.jpg") ==
          PHOTOC_IMAGE_IO_ERROR);
    CHECK(failure.attempted);
    CHECK(review_score_get(&failure, PHOTOC_REVIEW_FIXTURES "/sharp.jpg") ==
          PHOTOC_IMAGE_IO_ERROR);
    review_score_fail(&failure, PHOTOC_IMAGE_INVALID_JPEG);
    CHECK(failure.result == PHOTOC_IMAGE_IO_ERROR);

    review_score_cache prefailed = {0};
    review_score_fail(&prefailed, PHOTOC_IMAGE_IO_ERROR);
    CHECK(review_score_get(&prefailed, PHOTOC_REVIEW_FIXTURES "/sharp.jpg") ==
          PHOTOC_IMAGE_IO_ERROR);
    FILE *stream = tmpfile();
    CHECK(stream != NULL);
    review_display_sharpness(stream, &prefailed);
    char *text = contents(stream);
    CHECK(text != NULL && strcmp(text, "Sharpness: unavailable\n") == 0);
    free(text);
    fclose(stream);
    return 0;
}

int main(void)
{
    if (metadata_display() != 0 || sharpness_cache() != 0)
        return 1;
    return 0;
}
