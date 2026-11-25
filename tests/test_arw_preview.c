#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/arw.h"
#include "photoc/fs.h"
#include "photoc/image.h"
#include "photoc/jpeg_check.h"
#include "photoc/photo_image.h"
#include "photoc/sharpness.h"

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

static char directory[] = "/tmp/photoc-arw-preview-XXXXXX";

static char *join(const char *base, const char *name)
{
    char *path = NULL;
    CHECK(photoc_fs_join(base, name, &path) == 0);
    return path;
}

/* The caller frees *bytes. */
static size_t slurp(const char *path, unsigned char **bytes)
{
    size_t length = 0;
    *bytes = NULL;
    CHECK(photoc_image_read_file(path, 0, bytes, &length) == PHOTOC_IMAGE_OK);
    return length;
}

static char *write_temporary(const char *name, const unsigned char *bytes,
                             size_t length)
{
    char *path = join(directory, name);
    FILE *file = path == NULL ? NULL : fopen(path, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(fwrite(bytes, 1, length, file) == length);
        CHECK(fclose(file) == 0);
    }
    return path;
}

static uint32_t read_long_le(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

/* Offset of an IFD entry with tag in a little-endian fixture, or 0. */
static size_t ifd_entry(const unsigned char *bytes, size_t ifd, uint16_t tag)
{
    unsigned int count = (unsigned int)bytes[ifd] | bytes[ifd + 1] << 8;
    for (unsigned int i = 0; i < count; ++i) {
        size_t entry = ifd + 2 + i * 12u;
        if (((unsigned int)bytes[entry] | bytes[entry + 1] << 8) == tag)
            return entry;
    }
    return 0;
}

/* Offset of an IFD0 entry with tag in a little-endian fixture, or 0. */
static size_t root_entry(const unsigned char *bytes, uint16_t tag)
{
    return ifd_entry(bytes, read_long_le(bytes + 4), tag);
}

/* Next-IFD pointer after a little-endian IFD, or 0. */
static size_t next_ifd(const unsigned char *bytes, size_t ifd)
{
    unsigned int count = (unsigned int)bytes[ifd] | bytes[ifd + 1] << 8;
    return read_long_le(bytes + ifd + 2 + count * 12u);
}

static void zero_long(unsigned char *bytes, size_t entry)
{
    bytes[entry + 8] = 0;
    bytes[entry + 9] = 0;
    bytes[entry + 10] = 0;
    bytes[entry + 11] = 0;
}

static void test_read_preview(void)
{
    char *fixture = join(PHOTOC_ARW_FIXTURES, "preview.ARW");
    char *expected_path = join(PHOTOC_JPEG_FIXTURES, "blurred.jpg");
    unsigned char *expected = NULL;
    size_t expected_length = slurp(expected_path, &expected);
    unsigned char *preview = NULL;
    size_t length = 0;
    CHECK(photoc_arw_read_preview(fixture, 0, &preview, &length) ==
          PHOTOC_IMAGE_OK);
    /* The larger embedded JPEG wins over the IFD1 thumbnail. */
    CHECK(length == expected_length && preview != NULL &&
          memcmp(preview, expected, length) == 0);
    free(preview);
    preview = NULL;
    CHECK(photoc_arw_read_preview(fixture, expected_length - 1, &preview,
                                  &length) == PHOTOC_IMAGE_TOO_LARGE);
    CHECK(preview == NULL && length == 0);

    char *no_preview = join(PHOTOC_ARW_FIXTURES, "sony-le.ARW");
    CHECK(photoc_arw_read_preview(no_preview, 0, &preview, &length) ==
          PHOTOC_IMAGE_NO_PREVIEW);
    CHECK(photoc_arw_read_preview("/nonexistent/photoc.arw", 0, &preview,
                                  &length) == PHOTOC_IMAGE_IO_ERROR);
    CHECK(photoc_arw_read_preview(NULL, 0, &preview, &length) ==
          PHOTOC_IMAGE_INVALID_ARGUMENT);

    unsigned char *source = NULL;
    size_t source_length = slurp(fixture, &source);
    size_t offset_entry = source == NULL ? 0 : root_entry(source, 0x0201);
    CHECK(offset_entry != 0);
    if (offset_entry != 0) {
        uint32_t offset = read_long_le(source + offset_entry + 8);
        unsigned char *changed = malloc(source_length);
        CHECK(changed != NULL);
        if (changed != NULL) {
            memcpy(changed, source, source_length);
            changed[offset] = 0x00; /* Not a JPEG SOI. */
            char *path =
                write_temporary("not-jpeg.arw", changed, source_length);
            CHECK(photoc_arw_read_preview(path, 0, &preview, &length) ==
                  PHOTOC_IMAGE_INVALID_ARW);
            unlink(path);
            free(path);
            memcpy(changed, source, source_length);
            changed[offset_entry + 8] = 0xff; /* Offset past the end. */
            changed[offset_entry + 9] = 0xff;
            changed[offset_entry + 10] = 0xff;
            path = write_temporary("past-end.arw", changed, source_length);
            CHECK(photoc_arw_read_preview(path, 0, &preview, &length) ==
                  PHOTOC_IMAGE_INVALID_ARW);
            unlink(path);
            free(path);
            /* Present tags with offset 0 are invalid, not a missing preview.
               Zero both IFD0 and IFD1 so a leftover thumbnail cannot win. */
            size_t root = read_long_le(source + 4);
            size_t ifd1 = next_ifd(source, root);
            size_t ifd1_offset =
                ifd1 == 0 ? 0 : ifd_entry(source, ifd1, 0x0201);
            size_t ifd1_length =
                ifd1 == 0 ? 0 : ifd_entry(source, ifd1, 0x0202);
            CHECK(ifd1_offset != 0 && ifd1_length != 0);
            memcpy(changed, source, source_length);
            zero_long(changed, offset_entry);
            zero_long(changed, ifd1_offset);
            path = write_temporary("offset-zero.arw", changed, source_length);
            CHECK(photoc_arw_read_preview(path, 0, &preview, &length) ==
                  PHOTOC_IMAGE_INVALID_ARW);
            unlink(path);
            free(path);
            /* A present zero-length pair is invalid, not a missing preview. */
            size_t length_entry = root_entry(source, 0x0202);
            CHECK(length_entry != 0);
            memcpy(changed, source, source_length);
            zero_long(changed, length_entry);
            zero_long(changed, ifd1_length);
            path = write_temporary("zero-length.arw", changed, source_length);
            CHECK(photoc_arw_read_preview(path, 0, &preview, &length) ==
                  PHOTOC_IMAGE_INVALID_ARW);
            unlink(path);
            free(path);
            free(changed);
        }
        /* A prefix that cuts into the preview fails cleanly; a longer one
           still returns the complete preview. */
        size_t preview_end =
            read_long_le(source + offset_entry + 8) + expected_length;
        for (size_t size = 0; size < source_length; size += 97) {
            char *path = write_temporary("prefix.arw", source, size);
            photoc_image_result result =
                photoc_arw_read_preview(path, 0, &preview, &length);
            if (size < preview_end) {
                CHECK(result != PHOTOC_IMAGE_OK && preview == NULL);
            } else {
                CHECK(result == PHOTOC_IMAGE_OK && length == expected_length &&
                      memcmp(preview, expected, length) == 0);
            }
            free(preview);
            preview = NULL;
            unlink(path);
            free(path);
        }
    }
    static const unsigned char garbage[] = "garbage, not a TIFF container";
    char *path = write_temporary("garbage.arw", garbage, sizeof(garbage));
    CHECK(photoc_arw_read_preview(path, 0, &preview, &length) ==
          PHOTOC_IMAGE_INVALID_ARW);
    unlink(path);
    free(path);
    free(source);
    free(expected);
    free(expected_path);
    free(no_preview);
    free(fixture);
}

static void test_decode_and_score(void)
{
    char *fixture = join(PHOTOC_ARW_FIXTURES, "preview.ARW");
    char *jpeg = join(PHOTOC_JPEG_FIXTURES, "blurred.jpg");
    photoc_image image = {0};
    CHECK(photoc_photo_decode_scaled_bounded_with(NULL, fixture, 1600,
                                                  UINT64_C(1) << 20,
                                                  &image) == PHOTOC_IMAGE_OK);
    CHECK(image.width == 128 && image.height == 128);
    photoc_image_cleanup(&image);
    CHECK(photoc_photo_decode_scaled_bounded_with(
              NULL, fixture, 64, UINT64_C(1) << 20, &image) == PHOTOC_IMAGE_OK);
    CHECK(image.width == 64 && image.height == 64);
    photoc_image_cleanup(&image);
    /* The byte cap applies to the preview, not to the whole ARW. */
    CHECK(photoc_photo_decode_scaled_bounded_with(
              NULL, fixture, 1600, 100, &image) == PHOTOC_IMAGE_TOO_LARGE);
    CHECK(image.pixels == NULL);

    double arw_score = -1.0;
    double jpeg_score = -2.0;
    CHECK(photoc_sharpness_score_photo(fixture, 1024, &arw_score) ==
          PHOTOC_IMAGE_OK);
    CHECK(photoc_sharpness_score_jpeg(jpeg, 1024, &jpeg_score) ==
          PHOTOC_IMAGE_OK);
    /* A preview smaller than the matched analysis size is scored as is. */
    CHECK(arw_score == jpeg_score);
    CHECK(photoc_sharpness_score_photo(jpeg, 1024, &arw_score) ==
          PHOTOC_IMAGE_OK);
    CHECK(arw_score == jpeg_score);
    char *no_preview = join(PHOTOC_ARW_FIXTURES, "sony-le.ARW");
    CHECK(photoc_sharpness_score_photo(no_preview, 1024, &arw_score) ==
          PHOTOC_IMAGE_NO_PREVIEW);
    free(no_preview);
    free(jpeg);
    free(fixture);
}

static void test_scaled_dimensions_and_resize(void)
{
    uint32_t width = 0;
    uint32_t height = 0;
    CHECK(photoc_image_scaled_dimensions(5472, 3648, 1024, &width, &height) ==
          PHOTOC_IMAGE_OK);
    CHECK(width == 684 && height == 456);
    CHECK(photoc_image_scaled_dimensions(6000, 4000, 1024, &width, &height) ==
          PHOTOC_IMAGE_OK);
    CHECK(width == 750 && height == 500);
    CHECK(photoc_image_scaled_dimensions(128, 128, 1024, &width, &height) ==
          PHOTOC_IMAGE_OK);
    CHECK(width == 128 && height == 128);
    CHECK(photoc_image_scaled_dimensions(0, 10, 1024, &width, &height) ==
          PHOTOC_IMAGE_INVALID_ARGUMENT);

    unsigned char pixels[3 * 3] = {0, 0, 0, 90, 90, 90, 180, 180, 180};
    photoc_image source = {3, 1, 9, 9, pixels};
    photoc_image resized = {0};
    CHECK(photoc_image_resize_area(&source, 2, 1, &resized) == PHOTOC_IMAGE_OK);
    /* Output pixels cover 1.5 source pixels each. */
    CHECK(resized.pixels != NULL && resized.pixels[0] == 30 &&
          resized.pixels[3] == 150);
    photoc_image_cleanup(&resized);
    CHECK(photoc_image_resize_area(&source, 0, 1, &resized) ==
          PHOTOC_IMAGE_INVALID_ARGUMENT);
}

static void test_check(void)
{
    char *fixture = join(PHOTOC_ARW_FIXTURES, "preview.ARW");
    photoc_check_result result = photoc_arw_check_file(fixture);
    CHECK(result.status == PHOTOC_CHECK_OK &&
          result.code == PHOTOC_CHECK_ARW_READABLE);
    char *metadata_only = join(PHOTOC_ARW_FIXTURES, "sony-le.ARW");
    result = photoc_arw_check_file(metadata_only);
    CHECK(result.status == PHOTOC_CHECK_ERROR &&
          result.code == PHOTOC_CHECK_MISSING_SENSOR_DATA);
    unsigned char *source = NULL;
    size_t length = slurp(fixture, &source);
    if (source != NULL) {
        char *path = write_temporary("truncated.arw", source, length - 100);
        result = photoc_arw_check_file(path);
        CHECK(result.status == PHOTOC_CHECK_ERROR &&
              result.code == PHOTOC_CHECK_TRUNCATED_SENSOR_DATA);
        unlink(path);
        free(path);
        path = write_temporary("empty.arw", source, 0);
        result = photoc_arw_check_file(path);
        CHECK(result.code == PHOTOC_CHECK_EMPTY_FILE);
        unlink(path);
        free(path);
    }
    free(source);
    free(metadata_only);
    free(fixture);
}

static void test_open_regular(void)
{
    FILE *file = NULL;
    uint64_t size = 0;
    errno = 0;
    CHECK(photoc_fs_open_regular(directory, &file, &size) == -1 &&
          errno == EISDIR);
    char *target = join(PHOTOC_ARW_FIXTURES, "preview.ARW");
    char *link = join(directory, "link.arw");
    CHECK(target != NULL && link != NULL && symlink(target, link) == 0);
    errno = 0;
    CHECK(photoc_fs_open_regular(link, &file, &size) == -1 && errno == EINVAL);
    CHECK(photoc_fs_open_regular(target, &file, &size) == 0 && size > 0);
    if (file != NULL)
        CHECK(fclose(file) == 0);
    unlink(link);
    free(link);
    free(target);
}

int main(void)
{
    if (mkdtemp(directory) == NULL) {
        perror("mkdtemp");
        return 1;
    }
    test_read_preview();
    test_decode_and_score();
    test_scaled_dimensions_and_resize();
    test_check();
    test_open_regular();
    CHECK(rmdir(directory) == 0);
    return failures == 0 ? 0 : 1;
}
