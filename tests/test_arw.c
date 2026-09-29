#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/photo.h"
#include "photoc/fs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static char *fixture(const char *name)
{
    char *path = NULL;
    CHECK(photoc_fs_join(PHOTOC_ARW_FIXTURES, name, &path) == 0);
    return path;
}

static void test_common(const char *name, uint32_t width, uint32_t height)
{
    char *path = fixture(name);
    if (path == NULL)
        return;
    Photo photo = {0};
    CHECK(photo_load_metadata(path, &photo) == PHOTOC_METADATA_OK);
    CHECK(photo.format == PHOTOC_FORMAT_SONY_ARW);
    CHECK(photo.has_file_size && photo.file_size > 0);
    CHECK(photo.has_width && photo.width == width);
    CHECK(photo.has_height && photo.height == height);
    CHECK(photo.has_orientation && photo.orientation == 6);
    CHECK(photo.camera_make != NULL && strcmp(photo.camera_make, "SONY") == 0);
    CHECK(photo.camera_model != NULL &&
          strcmp(photo.camera_model, "DSC-RX100M7A") == 0);
    CHECK(photo.capture_timestamp != NULL &&
          strcmp(photo.capture_timestamp, "2026:09:29 12:34:56") == 0);
    CHECK(photo.has_iso && photo.iso == 1600);
    CHECK(photo.has_aperture && photo.aperture == 4);
    CHECK(photo.has_focal_length && photo.focal_length == 50);
    CHECK(photo.has_exposure_time && photo.exposure_time == 0.005);
    CHECK(photo.has_gps && photo.latitude < -37.8 && photo.latitude > -37.9);
    CHECK(photo.longitude < -122.4 && photo.longitude > -122.5);
    photo_cleanup(&photo);
    CHECK(photo.path == NULL && photo.format == PHOTOC_FORMAT_UNKNOWN);
    free(path);
}

static void test_missing(void)
{
    char *path = fixture("missing.arw");
    if (path == NULL)
        return;
    Photo photo = {0};
    CHECK(photo_load_metadata(path, &photo) == PHOTOC_METADATA_OK);
    CHECK(photo.format == PHOTOC_FORMAT_SONY_ARW);
    CHECK(photo.camera_make != NULL && strcmp(photo.camera_make, "SONY") == 0);
    CHECK(photo.camera_model == NULL && photo.capture_timestamp == NULL);
    CHECK(!photo.has_width && !photo.has_height && !photo.has_orientation);
    CHECK(!photo.has_iso && !photo.has_aperture && !photo.has_exposure_time);
    CHECK(!photo.has_focal_length && !photo.has_gps);
    photo_cleanup(&photo);
    free(path);
}

static void test_discovery(void)
{
    CHECK(photoc_format_from_path("DSC00001.ARW") == PHOTOC_FORMAT_SONY_ARW);
    CHECK(photoc_format_from_path("DSC00001.aRw") == PHOTOC_FORMAT_SONY_ARW);
    CHECK(photoc_format_from_path("image.JPEG") == PHOTOC_FORMAT_JPEG);
    CHECK(photoc_format_from_path("image.nef") == PHOTOC_FORMAT_UNKNOWN);
    CHECK(photoc_format_from_path("image.cr2") == PHOTOC_FORMAT_UNKNOWN);
    CHECK(photoc_format_from_path("image.dng") == PHOTOC_FORMAT_UNKNOWN);
    CHECK(photoc_format_from_path(NULL) == PHOTOC_FORMAT_UNKNOWN);
    CHECK(photoc_format_is_selected("image.arw", PHOTOC_FORMATS_METADATA));
    CHECK(!photoc_format_is_selected("image.arw", PHOTOC_FORMATS_JPEG));
    CHECK(!photoc_fs_is_jpeg("image.arw"));
}

static void test_every_prefix(void)
{
    char *path = fixture("sony-le.ARW");
    if (path == NULL)
        return;
    FILE *source = fopen(path, "rb");
    free(path);
    CHECK(source != NULL);
    if (source == NULL)
        return;
    unsigned char bytes[1024];
    size_t length = fread(bytes, 1, sizeof(bytes), source);
    CHECK(length > 0 && length < sizeof(bytes));
    CHECK(fclose(source) == 0);
    char directory[] = "/tmp/photoc-arw-prefix-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char *target = NULL;
    CHECK(photoc_fs_join(directory, "prefix.arw", &target) == 0);
    if (target != NULL) {
        for (size_t size = 0; size < length; ++size) {
            FILE *file = fopen(target, "wb");
            CHECK(file != NULL);
            if (file == NULL)
                break;
            CHECK(fwrite(bytes, 1, size, file) == size);
            CHECK(fclose(file) == 0);
            Photo photo = {0};
            CHECK(photo_load_metadata(target, &photo) ==
                  PHOTOC_METADATA_INVALID_ARW);
            CHECK(photo.path == NULL && photo.camera_make == NULL);
            photo_cleanup(&photo);
        }
        CHECK(unlink(target) == 0);
    }
    free(target);
    CHECK(rmdir(directory) == 0);
}

int main(void)
{
    test_common("sony-le.ARW", 5472, 3648);
    test_common("sony-be.arw", 5472, 3648);
    test_common("raw-dimensions.arw", 5504, 3664);
    test_missing();
    test_discovery();
    test_every_prefix();
    return failures == 0 ? 0 : 1;
}
