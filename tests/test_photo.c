#include "photoc/photo.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static char *copy_text(const char *text)
{
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, text, length + 1);
    }
    return copy;
}

static void check_empty_metadata(const Photo *photo)
{
    CHECK(photo->file_size == 0 && !photo->has_file_size);
    CHECK(photo->width == 0 && !photo->has_width);
    CHECK(photo->height == 0 && !photo->has_height);
    CHECK(photo->camera_make == NULL);
    CHECK(photo->camera_model == NULL);
    CHECK(photo->lens_model == NULL);
    CHECK(photo->capture_timestamp == NULL);
    CHECK(photo->iso == 0 && !photo->has_iso);
    CHECK(photo->aperture == 0.0 && !photo->has_aperture);
    CHECK(photo->exposure_time == 0.0 && !photo->has_exposure_time);
    CHECK(photo->focal_length == 0.0 && !photo->has_focal_length);
    CHECK(photo->focal_length_35mm == 0 && !photo->has_focal_length_35mm);
    CHECK(!photo->has_gps);
    CHECK(photo->latitude == 0.0 && photo->longitude == 0.0);
}

static void test_init_and_path_ownership(void)
{
    char source_path[] = "photos/image.jpg";
    Photo photo = {0};
    photo.width = 999;
    photo.has_width = true;

    if (photo_init(&photo, source_path) != 0) {
        CHECK(false);
        return;
    }
    CHECK(photo.path != source_path);
    CHECK(strcmp(photo.path, "photos/image.jpg") == 0);
    source_path[0] = 'x';
    CHECK(strcmp(photo.path, "photos/image.jpg") == 0);
    check_empty_metadata(&photo);

    photo_cleanup(&photo);
    CHECK(photo.path == NULL);
    check_empty_metadata(&photo);
    photo_cleanup(&photo);
    CHECK(photo.path == NULL);
}

static void test_cleanup_of_optional_strings(void)
{
    Photo photo = {0};
    if (photo_init(&photo, "image.jpg") != 0) {
        CHECK(false);
        return;
    }

    photo.camera_make = copy_text("Example Camera Co.");
    photo.camera_model = copy_text("Model A");
    photo.lens_model = copy_text("Standard EXIF Lens");
    photo.capture_timestamp = copy_text("2026:09:27 12:34:56");
    CHECK(photo.camera_make != NULL);
    CHECK(photo.camera_model != NULL);
    CHECK(photo.lens_model != NULL);
    CHECK(photo.capture_timestamp != NULL);

    photo.file_size = 0;
    photo.has_file_size = true; /* An empty file still has a known size. */
    photo.width = 6000;
    photo.has_width = true;
    photo.height = 4000;
    photo.has_height = true;
    photo.iso = 200;
    photo.has_iso = true;
    photo.aperture = 2.8;
    photo.has_aperture = true;
    photo.exposure_time = 0.01;
    photo.has_exposure_time = true;
    photo.focal_length = 50.0;
    photo.has_focal_length = true;
    photo.focal_length_35mm = 75;
    photo.has_focal_length_35mm = true;
    photo.has_gps = true;
    photo.latitude = 0.0; /* Zero coordinates are valid when has_gps is true. */
    photo.longitude = 11.5;

    photo_cleanup(&photo);
    CHECK(photo.path == NULL);
    check_empty_metadata(&photo);
    photo_cleanup(NULL);
}

static void test_invalid_initialization(void)
{
    Photo photo = {0};
    errno = 0;
    CHECK(photo_init(NULL, "image.jpg") == -1 && errno == EINVAL);
    errno = 0;
    CHECK(photo_init(&photo, NULL) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(photo_init(&photo, "") == -1 && errno == EINVAL);
    CHECK(photo.path == NULL);
    check_empty_metadata(&photo);

    CHECK(photo_init(&photo, "valid.jpg") == 0);
    char *original_path = photo.path;
    errno = 0;
    CHECK(photo_init(&photo, "") == -1 && errno == EINVAL);
    CHECK(photo.path == original_path);
    photo_cleanup(&photo);
}

int main(void)
{
    test_init_and_path_ownership();
    test_cleanup_of_optional_strings();
    test_invalid_initialization();

    if (failures != 0) {
        fprintf(stderr, "%d Photo test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
