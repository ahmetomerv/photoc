#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/fs.h"
#include "photoc/hash.h"
#include "photoc/jpeg_write.h"
#include "photoc/photo.h"

#include <errno.h>
#include <libexif/exif-data.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: %s (errno=%d)\n", __FILE__, __LINE__,    \
                    #condition, errno);                                        \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

static char *path_join(const char *directory, const char *name)
{
    char *path = NULL;
    if (photoc_fs_join(directory, name, &path) != 0) {
        CHECK(false);
    }
    return path;
}

static unsigned char *read_file(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    unsigned char *bytes = malloc((size_t)size + 1);
    if (bytes == NULL) {
        fclose(file);
        return NULL;
    }
    bool read_ok = fread(bytes, 1, (size_t)size, file) == (size_t)size;
    int close_result = fclose(file);
    if (!read_ok || close_result != 0) {
        free(bytes);
        return NULL;
    }
    *length = (size_t)size;
    return bytes;
}

static int write_file(const char *path, const void *bytes, size_t length)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }
    int result = fwrite(bytes, 1, length, file) == length ? 0 : -1;
    if (fclose(file) != 0) {
        result = -1;
    }
    return result;
}

static size_t after_first_app1(const unsigned char *bytes, size_t length)
{
    if (length < 6 || bytes[0] != 0xff || bytes[1] != 0xd8 ||
        bytes[2] != 0xff || bytes[3] != 0xe1) {
        return 0;
    }
    size_t end = 4 + ((size_t)bytes[4] << 8) + bytes[5];
    return end <= length ? end : 0;
}

static bool find_exif_segment(const unsigned char *bytes, size_t length,
                              size_t *start, size_t *end)
{
    if (length < 2 || bytes[0] != 0xff || bytes[1] != 0xd8) {
        return false;
    }
    for (size_t position = 2; position + 4 <= length;) {
        if (bytes[position] != 0xff) {
            return false;
        }
        unsigned char marker = bytes[position + 1];
        if (marker == 0xda) {
            return false;
        }
        size_t segment_end = position + 2 +
                             ((size_t)bytes[position + 2] << 8) +
                             bytes[position + 3];
        if (segment_end > length || segment_end <= position + 4) {
            return false;
        }
        if (marker == 0xe1 && segment_end >= position + 10 &&
            memcmp(bytes + position + 4, "Exif\0\0", 6) == 0) {
            *start = position;
            *end = segment_end;
            return true;
        }
        position = segment_end;
    }
    return false;
}

static void check_non_gps_metadata(const char *path, bool gps)
{
    Photo photo = {0};
    CHECK(photo_load_metadata(path, &photo) == PHOTOC_METADATA_OK);
    if (photo.path != NULL) {
        CHECK(photo.has_width && photo.width == 3);
        CHECK(photo.has_height && photo.height == 2);
        CHECK(photo.camera_make != NULL &&
              strcmp(photo.camera_make, "Fixture Camera Co.") == 0);
        CHECK(photo.camera_model != NULL &&
              strcmp(photo.camera_model, "Model Z") == 0);
        CHECK(photo.capture_timestamp != NULL &&
              strcmp(photo.capture_timestamp, "2026:09:27 12:34:56") == 0);
        CHECK(photo.has_iso && photo.iso == 200);
        CHECK(photo.has_aperture && photo.aperture > 2.79 &&
              photo.aperture < 2.81);
        CHECK(photo.has_exposure_time && photo.exposure_time > 0.0079 &&
              photo.exposure_time < 0.0081);
        CHECK(photo.has_focal_length && photo.focal_length == 50.0);
        CHECK(photo.has_gps == gps);
    }
    photo_cleanup(&photo);
    ExifData *data = exif_data_new_from_file(path);
    CHECK(data != NULL);
    if (data != NULL) {
        CHECK((data->ifd[EXIF_IFD_GPS]->count != 0) == gps);
        if (!gps) {
            CHECK(exif_content_get_entry(data->ifd[EXIF_IFD_0],
                    EXIF_TAG_GPS_INFO_IFD_POINTER) == NULL);
        }
        exif_data_unref(data);
    }
}

static void check_image_bytes(const char *written_path, const char *base_path)
{
    size_t base_length = 0;
    size_t written_length = 0;
    unsigned char *base = read_file(base_path, &base_length);
    unsigned char *written = read_file(written_path, &written_length);
    CHECK(base != NULL && written != NULL);
    if (base != NULL && written != NULL) {
        size_t start = 0;
        size_t end = 0;
        CHECK(find_exif_segment(written, written_length, &start, &end));
        CHECK(base_length >= start && written_length >= end);
        if (base_length >= start && written_length >= end && end > start) {
            CHECK(memcmp(written, base, start) == 0);
            CHECK(written_length - end == base_length - start);
            if (written_length - end == base_length - start) {
                CHECK(memcmp(written + end, base + start,
                             base_length - start) == 0);
            }
        }
    }
    free(base);
    free(written);
}

static void test_writing(const char *gps_path, const char *base_path,
                         const char *directory)
{
    char *clean_path = path_join(directory, "clean.jpg");
    char *copied_path = path_join(directory, "copied.jpg");
    char *inserted_path = path_join(directory, "inserted.jpg");
    char *existing_path = path_join(directory, "existing.jpg");
    CHECK(clean_path && copied_path && inserted_path && existing_path);
    if (!clean_path || !copied_path || !inserted_path || !existing_path) {
        goto cleanup;
    }

    unsigned char before[PHOTOC_SHA256_DIGEST_SIZE];
    unsigned char after[PHOTOC_SHA256_DIGEST_SIZE];
    CHECK(photoc_hash_file_sha256(gps_path, before) == 0);
    photoc_jpeg_exif *source = NULL;
    photoc_jpeg_exif *edited = NULL;
    CHECK(photoc_jpeg_exif_load_copy(gps_path, &source) == PHOTOC_JPEG_EDIT_OK);
    if (source == NULL) goto cleanup;
    CHECK(photoc_jpeg_exif_has_gps(source));
    CHECK(photoc_jpeg_exif_copy(source, &edited) == PHOTOC_JPEG_EDIT_OK);
    if (edited == NULL) {
        photoc_jpeg_exif_free(source);
        goto cleanup;
    }
    CHECK(photoc_jpeg_exif_remove_gps(edited) == PHOTOC_JPEG_EDIT_OK);
    CHECK(!photoc_jpeg_exif_has_gps(edited));
    CHECK(photoc_jpeg_exif_remove_gps(edited) == PHOTOC_JPEG_EDIT_OK);
    CHECK(photoc_jpeg_write_with_exif(gps_path, clean_path, edited) ==
          PHOTOC_JPEG_EDIT_OK);
    struct stat output_info;
    if (stat(clean_path, &output_info) == 0) {
        CHECK((output_info.st_mode & 0777) == 0600);
    } else {
        CHECK(false);
    }
    check_non_gps_metadata(clean_path, false);
    check_image_bytes(clean_path, base_path);
    CHECK(photoc_hash_file_sha256(gps_path, after) == 0);
    CHECK(memcmp(before, after, sizeof(before)) == 0);
    check_non_gps_metadata(gps_path, true);

    CHECK(photoc_jpeg_write_with_exif(gps_path, copied_path, source) ==
          PHOTOC_JPEG_EDIT_OK);
    check_non_gps_metadata(copied_path, true);
    check_image_bytes(copied_path, base_path);

    CHECK(photoc_jpeg_write_with_exif(base_path, inserted_path, source) ==
          PHOTOC_JPEG_EDIT_OK);
    check_non_gps_metadata(inserted_path, true);
    check_image_bytes(inserted_path, base_path);

    CHECK(write_file(existing_path, "sentinel", 8) == 0);
    errno = 0;
    CHECK(photoc_jpeg_write_with_exif(gps_path, existing_path, edited) ==
          PHOTOC_JPEG_EDIT_IO_ERROR && errno == EEXIST);
    size_t existing_length = 0;
    unsigned char *existing = read_file(existing_path, &existing_length);
    CHECK(existing != NULL && existing_length == 8 &&
          memcmp(existing, "sentinel", 8) == 0);
    free(existing);

    photoc_jpeg_exif_free(edited);
    photoc_jpeg_exif_free(source);
cleanup:
    if (clean_path) unlink(clean_path);
    if (copied_path) unlink(copied_path);
    if (inserted_path) unlink(inserted_path);
    if (existing_path) unlink(existing_path);
    free(clean_path);
    free(copied_path);
    free(inserted_path);
    free(existing_path);
}

static void test_failures(const char *gps_path, const char *base_path,
                          const char *invalid_path, const char *directory)
{
    char *output = path_join(directory, "should-not-exist.jpg");
    char *missing = path_join(directory, "missing.jpg");
    char *double_exif = path_join(directory, "double-exif.jpg");
    char *link_path = path_join(directory, "existing-link.jpg");
    CHECK(output && missing && double_exif && link_path);
    if (!output || !missing || !double_exif || !link_path) goto cleanup;
    photoc_jpeg_exif *exif = NULL;
    CHECK(photoc_jpeg_exif_load_copy(base_path, &exif) ==
          PHOTOC_JPEG_EDIT_NO_EXIF);
    CHECK(exif == NULL);
    CHECK(photoc_jpeg_exif_load_copy(invalid_path, &exif) ==
          PHOTOC_JPEG_EDIT_INVALID_JPEG);
    CHECK(exif == NULL);
    CHECK(photoc_jpeg_exif_load_copy(gps_path, &exif) == PHOTOC_JPEG_EDIT_OK);
    if (exif == NULL) goto cleanup;
    CHECK(photoc_jpeg_write_with_exif(invalid_path, output, exif) ==
          PHOTOC_JPEG_EDIT_INVALID_JPEG);
    bool exists = true;
    CHECK(photoc_fs_exists(output, &exists) == 0 && !exists);
    errno = 0;
    CHECK(photoc_jpeg_write_with_exif(missing, output, exif) ==
          PHOTOC_JPEG_EDIT_IO_ERROR && errno == ENOENT);
    CHECK(photoc_fs_exists(output, &exists) == 0 && !exists);
    errno = 0;
    CHECK(photoc_jpeg_write_with_exif(gps_path, gps_path, exif) ==
          PHOTOC_JPEG_EDIT_IO_ERROR && errno == EEXIST);
    CHECK(symlink(gps_path, link_path) == 0);
    errno = 0;
    CHECK(photoc_jpeg_write_with_exif(gps_path, link_path, exif) ==
          PHOTOC_JPEG_EDIT_IO_ERROR && errno == EEXIST);
    size_t gps_length = 0;
    unsigned char *gps_bytes = read_file(gps_path, &gps_length);
    CHECK(gps_bytes != NULL);
    if (gps_bytes != NULL) {
        size_t segment_end = after_first_app1(gps_bytes, gps_length);
        CHECK(segment_end > 2);
        if (segment_end > 2) {
            size_t extra = segment_end - 2;
            unsigned char *twice = malloc(gps_length + extra);
            CHECK(twice != NULL);
            if (twice != NULL) {
                memcpy(twice, gps_bytes, segment_end);
                memcpy(twice + segment_end, gps_bytes + 2, extra);
                memcpy(twice + segment_end + extra, gps_bytes + segment_end,
                       gps_length - segment_end);
                CHECK(write_file(double_exif, twice, gps_length + extra) == 0);
                photoc_jpeg_exif *other = NULL;
                CHECK(photoc_jpeg_exif_load_copy(double_exif, &other) ==
                      PHOTOC_JPEG_EDIT_UNSAFE_LAYOUT);
                CHECK(other == NULL);
                CHECK(photoc_jpeg_write_with_exif(double_exif, output, exif) ==
                      PHOTOC_JPEG_EDIT_UNSAFE_LAYOUT);
                CHECK(photoc_fs_exists(output, &exists) == 0 && !exists);
                photoc_jpeg_exif_free(other);
                free(twice);
            }
        }
        free(gps_bytes);
    }
    CHECK(photoc_jpeg_exif_copy(NULL, &exif) ==
          PHOTOC_JPEG_EDIT_INVALID_ARGUMENT);
    CHECK(photoc_jpeg_exif_remove_gps(NULL) ==
          PHOTOC_JPEG_EDIT_INVALID_ARGUMENT);
    photoc_jpeg_exif_free(exif);
cleanup:
    if (output) unlink(output);
    if (double_exif) unlink(double_exif);
    if (link_path) unlink(link_path);
    free(output);
    free(missing);
    free(double_exif);
    free(link_path);
}

int main(void)
{
    char *gps = path_join(PHOTOC_METADATA_FIXTURES, "with_gps.jpeg");
    char *base = path_join(PHOTOC_METADATA_FIXTURES, "no_exif.jpg");
    char *invalid = path_join(PHOTOC_METADATA_FIXTURES, "invalid.jpg");
    const char *temporary = getenv("TMPDIR");
    if (temporary == NULL || temporary[0] == '\0') temporary = "/tmp";
    char *directory = path_join(temporary, "photoc-jpeg-write-XXXXXX");
    CHECK(gps && base && invalid && directory);
    if (gps && base && invalid && directory && mkdtemp(directory) != NULL) {
        test_writing(gps, base, directory);
        test_failures(gps, base, invalid, directory);
        CHECK(rmdir(directory) == 0);
    } else {
        CHECK(false);
    }
    free(directory);
    free(invalid);
    free(base);
    free(gps);
    if (failures != 0) {
        fprintf(stderr, "%d JPEG writing test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
