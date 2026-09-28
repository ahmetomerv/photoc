#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/fs.h"
#include "photoc/hash.h"
#include "photoc/image.h"
#include "photoc/jpeg_write.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
static int temporary_attempts;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

/* The template contents are unspecified on failure. Interpose only in this
   test executable to prove that cleanup cannot unlink an unowned pathname. */
int mkstemp(char *name)
{
    (void)name;
    ++temporary_attempts;
    errno = EEXIST;
    return -1;
}

static void protect_file(const char *path)
{
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(fputs("unrelated user data", file) != EOF);
        CHECK(fclose(file) == 0);
    }
}

static void check_protected(const char *path)
{
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    if (file != NULL) {
        char content[32] = {0};
        CHECK(fread(content, 1, sizeof(content), file) == 19);
        CHECK(strcmp(content, "unrelated user data") == 0);
        CHECK(fclose(file) == 0);
    }
}

int main(void)
{
    char directory[] = "/tmp/photoc-temp-failure-XXXXXX";
    if (mkdtemp(directory) == NULL) {
        return EXIT_FAILURE;
    }
    char *source = NULL;
    char *destination = NULL;
    char *metadata_temp = NULL;
    char *encoded_temp = NULL;
    photoc_jpeg_exif *exif = NULL;
    photoc_image image = {0};
    photoc_jpeg_buffer encoded = {0};
    CHECK(photoc_fs_join(directory, "source.jpg", &source) == 0);
    CHECK(photoc_fs_join(directory, "output.jpg", &destination) == 0);
    CHECK(photoc_fs_join(directory, ".photoc-exif-XXXXXX", &metadata_temp) ==
          0);
    CHECK(photoc_fs_join(directory, "output.jpg.photoc-XXXXXX",
                         &encoded_temp) == 0);
    if (source == NULL || destination == NULL || metadata_temp == NULL ||
        encoded_temp == NULL) {
        goto cleanup;
    }
    FILE *input = fopen(PHOTOC_METADATA_FIXTURES "/with_gps.jpeg", "rb");
    FILE *output = fopen(source, "wb");
    CHECK(input != NULL && output != NULL);
    if (input == NULL || output == NULL) {
        if (input != NULL)
            fclose(input);
        if (output != NULL)
            fclose(output);
        goto cleanup;
    }
    int byte;
    while ((byte = fgetc(input)) != EOF)
        CHECK(fputc(byte, output) != EOF);
    CHECK(!ferror(input));
    CHECK(fclose(input) == 0);
    CHECK(fclose(output) == 0);
    unsigned char before[32] = {0};
    unsigned char after[32] = {0};
    CHECK(photoc_hash_file_sha256(source, before) == 0);
    CHECK(photoc_jpeg_exif_load_copy(source, &exif) == PHOTOC_JPEG_EDIT_OK);
    CHECK(photoc_jpeg_exif_remove_gps(exif) == PHOTOC_JPEG_EDIT_OK);
    protect_file(metadata_temp);
    CHECK(photoc_jpeg_write_with_exif(source, destination, exif) ==
          PHOTOC_JPEG_EDIT_IO_ERROR);
    CHECK(errno == EEXIST);
    check_protected(metadata_temp);
    protect_file(metadata_temp);
    CHECK(photoc_jpeg_replace_with_exif(source, exif) ==
          PHOTOC_JPEG_EDIT_IO_ERROR);
    CHECK(errno == EEXIST);
    check_protected(metadata_temp);

    CHECK(photoc_image_decode_jpeg(source, &image) == PHOTOC_IMAGE_OK);
    CHECK(photoc_image_encode_jpeg(&image, 80, &encoded) == PHOTOC_IMAGE_OK);
    protect_file(encoded_temp);
    CHECK(photoc_jpeg_write_encoded(destination, &encoded, NULL) ==
          PHOTOC_JPEG_EDIT_IO_ERROR);
    CHECK(errno == EEXIST);
    check_protected(encoded_temp);
    CHECK(temporary_attempts == 3);
    CHECK(access(destination, F_OK) == -1 && errno == ENOENT);
    CHECK(photoc_hash_file_sha256(source, after) == 0);
    CHECK(memcmp(before, after, sizeof(before)) == 0);
cleanup:
    photoc_jpeg_buffer_cleanup(&encoded);
    photoc_image_cleanup(&image);
    photoc_jpeg_exif_free(exif);
    if (source != NULL)
        unlink(source);
    if (destination != NULL)
        unlink(destination);
    if (metadata_temp != NULL)
        unlink(metadata_temp);
    if (encoded_temp != NULL)
        unlink(encoded_temp);
    free(source);
    free(destination);
    free(metadata_temp);
    free(encoded_temp);
    CHECK(rmdir(directory) == 0);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
