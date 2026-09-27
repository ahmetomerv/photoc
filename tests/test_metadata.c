#include "photoc/photo.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int failures;

static int near(double actual, double expected)
{
    double difference = actual - expected;
    return difference > -0.00001 && difference < 0.00001;
}

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);   \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

static char *fixture(const char *name)
{
    size_t length = strlen(PHOTOC_METADATA_FIXTURES) + strlen(name) + 2;
    char *path = malloc(length);
    if (path != NULL) {
        snprintf(path, length, "%s/%s", PHOTOC_METADATA_FIXTURES, name);
    }
    return path;
}

static void check_base(const Photo *photo, const char *path)
{
    struct stat info;
    CHECK(stat(path, &info) == 0);
    CHECK(photo->path != path);
    CHECK(strcmp(photo->path, path) == 0);
    CHECK(photo->has_file_size);
    CHECK(photo->file_size == (uint64_t)info.st_size);
    CHECK(photo->has_width && photo->width == 3);
    CHECK(photo->has_height && photo->height == 2);
}

static void test_with_exif(void)
{
    char *path = fixture("with_exif.jpg");
    CHECK(path != NULL);
    if (path == NULL) {
        return;
    }
    Photo photo = {0};
    photoc_metadata_result result = photo_load_metadata(path, &photo);
    CHECK(result == PHOTOC_METADATA_OK);
    if (result == PHOTOC_METADATA_OK) {
        check_base(&photo, path);
        CHECK(photo.camera_make != NULL &&
              strcmp(photo.camera_make, "Fixture Camera Co.") == 0);
        CHECK(photo.camera_model != NULL &&
              strcmp(photo.camera_model, "Model Z") == 0);
        CHECK(photo.capture_timestamp != NULL &&
              strcmp(photo.capture_timestamp, "2026:09:27 12:34:56") == 0);
        CHECK(photo.has_iso && photo.iso == 200);
        CHECK(photo.has_aperture && near(photo.aperture, 2.8));
        CHECK(photo.has_exposure_time &&
              near(photo.exposure_time, 0.008));
        CHECK(photo.has_focal_length && photo.focal_length == 50.0);
        CHECK(!photo.has_gps);
    }
    photo_cleanup(&photo);
    free(path);
}

static void test_with_gps(void)
{
    char *path = fixture("with_gps.jpeg");
    CHECK(path != NULL);
    if (path == NULL) {
        return;
    }
    Photo photo = {0};
    photoc_metadata_result result = photo_load_metadata(path, &photo);
    CHECK(result == PHOTOC_METADATA_OK);
    if (result == PHOTOC_METADATA_OK) {
        check_base(&photo, path);
        CHECK(photo.has_gps);
        CHECK(near(photo.latitude, -37.8083333333));
        CHECK(near(photo.longitude, -122.4041666667));
    }
    photo_cleanup(&photo);
    free(path);
}

static void test_without_exif(void)
{
    char *path = fixture("no_exif.jpg");
    CHECK(path != NULL);
    if (path == NULL) {
        return;
    }
    Photo photo = {0};
    photoc_metadata_result result = photo_load_metadata(path, &photo);
    CHECK(result == PHOTOC_METADATA_OK);
    if (result == PHOTOC_METADATA_OK) {
        check_base(&photo, path);
        CHECK(photo.camera_make == NULL && photo.camera_model == NULL);
        CHECK(photo.capture_timestamp == NULL);
        CHECK(!photo.has_iso && !photo.has_aperture);
        CHECK(!photo.has_exposure_time && !photo.has_focal_length);
        CHECK(!photo.has_gps);
    }
    photo_cleanup(&photo);
    free(path);
}

static void test_failures(void)
{
    Photo photo = {0};
    char *invalid = fixture("invalid.jpg");
    char *unsupported = fixture("unsupported.png");
    char *missing = fixture("missing.jpg");
    CHECK(invalid != NULL && unsupported != NULL && missing != NULL);
    if (invalid != NULL && unsupported != NULL && missing != NULL) {
        CHECK(photo_load_metadata(invalid, &photo) ==
              PHOTOC_METADATA_INVALID_JPEG);
        CHECK(strcmp(photo_metadata_result_message(PHOTOC_METADATA_INVALID_JPEG),
                     "invalid or truncated JPEG file") == 0);
        CHECK(photo.path == NULL);
        CHECK(photo_load_metadata(unsupported, &photo) ==
              PHOTOC_METADATA_UNSUPPORTED_FORMAT);
        CHECK(strstr(photo_metadata_result_message(
                         PHOTOC_METADATA_UNSUPPORTED_FORMAT), ".jpg") != NULL);
        CHECK(photo.path == NULL);
        errno = 0;
        CHECK(photo_load_metadata(missing, &photo) == PHOTOC_METADATA_IO_ERROR);
        CHECK(errno == ENOENT);
        CHECK(photo.path == NULL);
        CHECK(photo_load_metadata(NULL, &photo) ==
              PHOTOC_METADATA_INVALID_ARGUMENT);
        CHECK(photo_load_metadata("", &photo) ==
              PHOTOC_METADATA_INVALID_ARGUMENT);
        CHECK(photo_load_metadata(invalid, NULL) ==
              PHOTOC_METADATA_INVALID_ARGUMENT);
    }
    free(invalid);
    free(unsupported);
    free(missing);
    photo_cleanup(&photo);
}

int main(void)
{
    test_with_exif();
    test_with_gps();
    test_without_exif();
    test_failures();
    if (failures != 0) {
        fprintf(stderr, "%d metadata test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
