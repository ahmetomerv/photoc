#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/photo.h"
#include "metadata_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

static int near(double actual, double expected)
{
    double difference = actual - expected;
    return difference > -0.00001 && difference < 0.00001;
}

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++failures;                                                        \
        }                                                                      \
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
        CHECK(photo.has_exposure_time && near(photo.exposure_time, 0.008));
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

static void *fail_exif_allocation(size_t size)
{
    (void)size;
    return NULL;
}

static void test_exif_capture_allocation_failure(void)
{
    char *path = fixture("with_exif.jpg");
    char *without_exif = fixture("no_exif.jpg");
    CHECK(path != NULL && without_exif != NULL);
    if (path != NULL && without_exif != NULL) {
        Photo photo = {0};
        int free_descriptor = dup(STDERR_FILENO);
        CHECK(free_descriptor >= 0);
        if (free_descriptor >= 0) {
            CHECK(close(free_descriptor) == 0);
        }
        errno = 0;
        CHECK(photoc_metadata_load_with_exif_allocator(path, &photo,
                                                       fail_exif_allocation) ==
              PHOTOC_METADATA_NO_MEMORY);
        CHECK(errno == ENOMEM);
        if (free_descriptor >= 0) {
            errno = 0;
            CHECK(fcntl(free_descriptor, F_GETFD) == -1 && errno == EBADF);
        }
        CHECK(photo.path == NULL);
        CHECK(photo.camera_make == NULL && photo.camera_model == NULL);
        CHECK(photo.capture_timestamp == NULL);
        CHECK(!photo.has_file_size && !photo.has_width && !photo.has_height);
        photo_cleanup(&photo);

        CHECK(photoc_metadata_load_with_exif_allocator(without_exif, &photo,
                                                       fail_exif_allocation) ==
              PHOTOC_METADATA_OK);
        photo_cleanup(&photo);
    }
    free(path);
    free(without_exif);
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
        CHECK(
            strcmp(photo_metadata_result_message(PHOTOC_METADATA_INVALID_JPEG),
                   "invalid or truncated JPEG file") == 0);
        CHECK(photo.path == NULL);
        CHECK(photo_load_metadata(unsupported, &photo) ==
              PHOTOC_METADATA_UNSUPPORTED_FORMAT);
        CHECK(strstr(photo_metadata_result_message(
                         PHOTOC_METADATA_UNSUPPORTED_FORMAT),
                     ".jpg") != NULL);
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

static void test_non_regular_and_empty(void)
{
    char directory[] = "photoc-metadata-XXXXXX";
    if (mkdtemp(directory) == NULL) {
        CHECK(false);
        return;
    }

    char album[512];
    char empty[512];
    char link[512];
    CHECK(snprintf(album, sizeof(album), "%s/album.jpg", directory) <
          (int)sizeof(album));
    CHECK(snprintf(empty, sizeof(empty), "%s/empty.jpg", directory) <
          (int)sizeof(empty));
    CHECK(snprintf(link, sizeof(link), "%s/link.jpg", directory) <
          (int)sizeof(link));

    Photo photo = {0};
    CHECK(mkdir(album, 0700) == 0);
    errno = 0;
    CHECK(photo_load_metadata(album, &photo) == PHOTOC_METADATA_IO_ERROR);
    CHECK(errno == EISDIR);
    CHECK(photo.path == NULL);

    FILE *file = fopen(empty, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(fclose(file) == 0);
    }
    errno = 0;
    CHECK(photo_load_metadata(empty, &photo) == PHOTOC_METADATA_INVALID_JPEG);
    CHECK(photo.path == NULL);

    char *target = fixture("with_exif.jpg");
    CHECK(target != NULL);
    if (target != NULL) {
        CHECK(symlink(target, link) == 0);
        errno = 0;
        CHECK(photo_load_metadata(link, &photo) == PHOTOC_METADATA_IO_ERROR);
        CHECK(errno == EINVAL);
        CHECK(photo.path == NULL);
        CHECK(unlink(link) == 0);
    }
    free(target);
    CHECK(unlink(empty) == 0);
    CHECK(rmdir(album) == 0);
    CHECK(rmdir(directory) == 0);
    photo_cleanup(&photo);
}

static int same_text(const char *left, const char *right)
{
    if (left == NULL || right == NULL) {
        return left == right;
    }
    return strcmp(left, right) == 0;
}

static void test_memory_matches_file(void)
{
    char *path = fixture("with_exif.jpg");
    CHECK(path != NULL);
    if (path == NULL) {
        return;
    }
    ExifData *reference = exif_data_new_from_file(path);
    CHECK(reference != NULL);
    Photo from_file = {0};
    if (reference != NULL) {
        CHECK(photoc_metadata_read_exif(reference, &from_file));
        exif_data_unref(reference);
    }
    Photo from_memory = {0};
    CHECK(photo_load_metadata(path, &from_memory) == PHOTOC_METADATA_OK);

    CHECK(same_text(from_file.camera_make, from_memory.camera_make));
    CHECK(same_text(from_file.camera_model, from_memory.camera_model));
    CHECK(same_text(from_file.lens_model, from_memory.lens_model));
    CHECK(
        same_text(from_file.capture_timestamp, from_memory.capture_timestamp));
    CHECK(from_file.has_iso == from_memory.has_iso);
    CHECK(from_file.iso == from_memory.iso);
    CHECK(from_file.has_aperture == from_memory.has_aperture);
    CHECK(near(from_file.aperture, from_memory.aperture));
    CHECK(from_file.has_exposure_time == from_memory.has_exposure_time);
    CHECK(near(from_file.exposure_time, from_memory.exposure_time));
    CHECK(from_file.has_focal_length == from_memory.has_focal_length);
    CHECK(from_file.focal_length == from_memory.focal_length);
    CHECK(from_file.has_focal_length_35mm == from_memory.has_focal_length_35mm);
    CHECK(from_file.focal_length_35mm == from_memory.focal_length_35mm);
    CHECK(from_file.has_orientation == from_memory.has_orientation);
    CHECK(from_file.orientation == from_memory.orientation);
    CHECK(from_file.has_gps == from_memory.has_gps);
    CHECK(near(from_file.latitude, from_memory.latitude));
    CHECK(near(from_file.longitude, from_memory.longitude));

    photo_cleanup(&from_file);
    photo_cleanup(&from_memory);
    free(path);
}

int main(void)
{
    test_with_exif();
    test_memory_matches_file();
    test_with_gps();
    test_without_exif();
    test_exif_capture_allocation_failure();
    test_failures();
    test_non_regular_and_empty();
    if (failures != 0) {
        fprintf(stderr, "%d metadata test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
