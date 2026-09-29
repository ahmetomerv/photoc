#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/fs.h"
#include "photoc/jpeg_metadata.h"

#include <errno.h>
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

static unsigned char *read_file(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
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
    bool ok = fread(bytes, 1, (size_t)size, file) == (size_t)size;
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        free(bytes);
        return NULL;
    }
    *length = (size_t)size;
    return bytes;
}

static bool write_file(const char *path, const void *bytes, size_t length)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    bool ok = fwrite(bytes, 1, length, file) == length;
    if (fclose(file) != 0) {
        ok = false;
    }
    return ok;
}

static void test_roundtrip(const char *destination)
{
    static const char *const fixtures[] = {
        "exif_only.jpg",    "icc_only.jpg",  "xmp_only.jpg",
        "all_metadata.jpg", "icc_multi.jpg", "no_metadata.jpg",
        "xmp_extended.jpg", "gray_icc.jpg"};
    uint64_t base_size = 0;
    CHECK(photoc_fs_file_size(PHOTOC_SEGMENT_FIXTURES "/no_metadata.jpg",
                              &base_size) == 0);
    for (size_t i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); ++i) {
        char *path = NULL;
        CHECK(photoc_fs_join(PHOTOC_SEGMENT_FIXTURES, fixtures[i], &path) == 0);
        if (path == NULL) {
            continue;
        }
        photoc_jpeg_metadata *metadata = NULL;
        photoc_image image = {0};
        photoc_jpeg_buffer encoded = {0};
        uint64_t source_size = 0;
        CHECK(photoc_jpeg_metadata_load_copy(path, &metadata) ==
              PHOTOC_JPEG_EDIT_OK);
        CHECK(photoc_fs_file_size(path, &source_size) == 0);
        uint64_t plain_size = base_size;
        if (photoc_jpeg_metadata_has_grayscale_icc(metadata)) {
            CHECK(photoc_fs_file_size(PHOTOC_SEGMENT_FIXTURES
                                      "/gray_no_metadata.jpg",
                                      &plain_size) == 0);
        }
        CHECK(photoc_jpeg_metadata_output_overhead(metadata) ==
              source_size - plain_size);
        CHECK(photoc_image_decode_jpeg(path, &image) == PHOTOC_IMAGE_OK);
        CHECK((photoc_jpeg_metadata_has_grayscale_icc(metadata)
                   ? photoc_image_encode_jpeg_grayscale(&image, 80, &encoded)
                   : photoc_image_encode_jpeg(&image, 80, &encoded)) ==
              PHOTOC_IMAGE_OK);
        if (metadata != NULL && encoded.data != NULL) {
            CHECK(photoc_jpeg_write_encoded_with_metadata(
                      destination, &encoded, metadata) == PHOTOC_JPEG_EDIT_OK);
            uint64_t output_size = 0;
            CHECK(photoc_fs_file_size(destination, &output_size) == 0);
            CHECK(output_size ==
                  encoded.size +
                      photoc_jpeg_metadata_output_overhead(metadata));
            photoc_image output = {0};
            CHECK(photoc_image_decode_jpeg(destination, &output) ==
                  PHOTOC_IMAGE_OK);
            CHECK(output.width == 3 && output.height == 2);
            photoc_image_cleanup(&output);
            CHECK(photoc_jpeg_write_encoded_with_metadata(destination, &encoded,
                                                          metadata) ==
                  PHOTOC_JPEG_EDIT_IO_ERROR);
            CHECK(errno == EEXIST);
            CHECK(unlink(destination) == 0);
            size_t full_size = encoded.size;
            encoded.size -= 2; /* Missing EOI: reject rather than publishing. */
            CHECK(photoc_jpeg_write_encoded_with_metadata(destination, &encoded,
                                                          metadata) ==
                  PHOTOC_JPEG_EDIT_INVALID_JPEG);
            CHECK(access(destination, F_OK) == -1 && errno == ENOENT);
            encoded.size = full_size;
        }
        photoc_jpeg_buffer_cleanup(&encoded);
        photoc_image_cleanup(&image);
        photoc_jpeg_metadata_free(metadata);
        free(path);
    }
}

static void test_output_verification(const char *destination)
{
    photoc_jpeg_metadata *metadata = NULL;
    CHECK(photoc_jpeg_metadata_load_copy(PHOTOC_SEGMENT_FIXTURES
                                         "/all_metadata.jpg",
                                         &metadata) == PHOTOC_JPEG_EDIT_OK);
    unsigned char pixels[12] = {0};
    photoc_image image = {4, 1, 12, sizeof(pixels),
                          pixels}; /* Borrowed stack pixels. */
    photoc_jpeg_buffer encoded = {0};
    CHECK(photoc_image_encode_jpeg(&image, 80, &encoded) == PHOTOC_IMAGE_OK);
    if (metadata != NULL && encoded.data != NULL) {
        CHECK(photoc_jpeg_write_encoded_with_metadata(destination, &encoded,
                                                      metadata) ==
              PHOTOC_JPEG_EDIT_INVALID_JPEG);
        CHECK(access(destination, F_OK) == -1 && errno == ENOENT);
    }
    photoc_jpeg_buffer_cleanup(&encoded);
    size_t size = 0;
    unsigned char *bytes =
        read_file(PHOTOC_SEGMENT_FIXTURES "/all_metadata.jpg", &size);
    if (bytes != NULL && metadata != NULL) {
        /* An already tagged encoded buffer would duplicate EXIF. */
        const photoc_jpeg_buffer tagged = {bytes, size};
        CHECK(photoc_jpeg_write_encoded_with_metadata(destination, &tagged,
                                                      metadata) ==
              PHOTOC_JPEG_EDIT_UNSAFE_LAYOUT);
        CHECK(access(destination, F_OK) == -1 && errno == ENOENT);
    }
    free(bytes);
    photoc_jpeg_metadata_free(metadata);
    metadata = NULL;
    CHECK(photoc_jpeg_metadata_load_copy(PHOTOC_SEGMENT_FIXTURES
                                         "/gray_icc.jpg",
                                         &metadata) == PHOTOC_JPEG_EDIT_OK);
    CHECK(photoc_jpeg_metadata_has_grayscale_icc(metadata));
    unsigned char gray_pixels[18] = {0};
    const photoc_image gray = {3, 2, 9, sizeof(gray_pixels), gray_pixels};
    CHECK(photoc_image_encode_jpeg(&gray, 80, &encoded) == PHOTOC_IMAGE_OK);
    if (metadata != NULL && encoded.data != NULL) {
        /* Matching dimensions are insufficient if a gray ICC is attached to RGB. */
        CHECK(photoc_jpeg_write_encoded_with_metadata(destination, &encoded,
                                                      metadata) ==
              PHOTOC_JPEG_EDIT_INVALID_JPEG);
        CHECK(access(destination, F_OK) == -1 && errno == ENOENT);
    }
    photoc_jpeg_buffer_cleanup(&encoded);
    photoc_jpeg_metadata_free(metadata);
}

static void test_truncation_and_lengths(const char *path)
{
    size_t length = 0;
    unsigned char *bytes =
        read_file(PHOTOC_SEGMENT_FIXTURES "/all_metadata.jpg", &length);
    CHECK(bytes != NULL);
    if (bytes == NULL) {
        return;
    }
    for (size_t i = 0; i < length; ++i) {
        CHECK(write_file(path, bytes, i));
        photoc_jpeg_metadata *metadata = NULL;
        CHECK(photoc_jpeg_metadata_load_copy(path, &metadata) !=
              PHOTOC_JPEG_EDIT_OK);
        CHECK(metadata == NULL);
        photoc_jpeg_metadata_free(metadata);
    }
    /* Corrupt the first APP1 length, exercising underflow and short reads. */
    const unsigned int lengths[] = {0, 1, 2, 65535};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        bytes[4] = (unsigned char)(lengths[i] >> 8);
        bytes[5] = (unsigned char)lengths[i];
        CHECK(write_file(path, bytes, length));
        photoc_jpeg_metadata *metadata = NULL;
        CHECK(photoc_jpeg_metadata_load_copy(path, &metadata) ==
              PHOTOC_JPEG_EDIT_INVALID_JPEG);
        CHECK(metadata == NULL);
    }
    free(bytes);
}

static void test_memory_limit(const char *path)
{
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file == NULL) {
        return;
    }
    const unsigned char soi[] = {0xff, 0xd8};
    const unsigned char header[] = {0xff, 0xe1, 0xff, 0xff};
    unsigned char payload[65533] = {0};
    memcpy(payload, "http://ns.adobe.com/xap/1.0/",
           sizeof("http://ns.adobe.com/xap/1.0/"));
    CHECK(fwrite(soi, 1, sizeof(soi), file) == sizeof(soi));
    for (size_t i = 0; i < 1100; ++i) {
        CHECK(fwrite(header, 1, sizeof(header), file) == sizeof(header));
        CHECK(fwrite(payload, 1, sizeof(payload), file) == sizeof(payload));
    }
    CHECK(fclose(file) == 0);
    photoc_jpeg_metadata *metadata = NULL;
    CHECK(photoc_jpeg_metadata_load_copy(path, &metadata) ==
          PHOTOC_JPEG_EDIT_METADATA_TOO_LARGE);
    CHECK(metadata == NULL);
}

int main(void)
{
    char directory[] = "photoc-marker-test-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    if (failures != 0) {
        return EXIT_FAILURE;
    }
    char *path = NULL;
    char *destination = NULL;
    CHECK(photoc_fs_join(directory, "input.jpg", &path) == 0);
    CHECK(photoc_fs_join(directory, "output.jpg", &destination) == 0);
    if (path != NULL && destination != NULL) {
        photoc_jpeg_metadata *metadata = NULL;
        CHECK(photoc_jpeg_metadata_load_copy(NULL, &metadata) ==
              PHOTOC_JPEG_EDIT_INVALID_ARGUMENT);
        CHECK(metadata == NULL);
        CHECK(photoc_jpeg_metadata_load_copy(path, NULL) ==
              PHOTOC_JPEG_EDIT_INVALID_ARGUMENT);
        CHECK(photoc_jpeg_metadata_output_overhead(NULL) == 0);
        photoc_jpeg_metadata_free(NULL);
        CHECK(
            photoc_jpeg_write_encoded_with_metadata(destination, NULL, NULL) ==
            PHOTOC_JPEG_EDIT_INVALID_ARGUMENT);
        CHECK(photoc_jpeg_metadata_load_copy(
                  PHOTOC_SEGMENT_FIXTURES "/icc_bad_sequence.jpg", &metadata) ==
              PHOTOC_JPEG_EDIT_INVALID_ICC);
        CHECK(metadata == NULL);
        test_roundtrip(destination);
        test_output_verification(destination);
        test_truncation_and_lengths(path);
        test_memory_limit(path);
        CHECK(unlink(path) == 0);
    }
    free(path);
    free(destination);
    CHECK(rmdir(directory) ==
          0); /* Also proves failed writes left no temporaries. */
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
