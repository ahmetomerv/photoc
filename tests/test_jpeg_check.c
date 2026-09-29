#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/jpeg_check.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <jpeglib.h>

static int failures;
#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static void make_jpeg(const char *path, bool progressive, bool cmyk)
{
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file == NULL)
        return;
    struct jpeg_compress_struct codec;
    struct jpeg_error_mgr error;
    codec.err = jpeg_std_error(&error);
    jpeg_create_compress(&codec);
    jpeg_stdio_dest(&codec, file);
    codec.image_width = 8;
    codec.image_height = 8;
    codec.input_components = cmyk ? 4 : 3;
    codec.in_color_space = cmyk ? JCS_CMYK : JCS_RGB;
    jpeg_set_defaults(&codec);
    if (progressive)
        jpeg_simple_progression(&codec);
    jpeg_start_compress(&codec, TRUE);
    unsigned char pixels[32];
    while (codec.next_scanline < codec.image_height) {
        for (size_t i = 0; i < sizeof(pixels); ++i) {
            pixels[i] = (unsigned char)(i * 7 + codec.next_scanline * 11);
        }
        JSAMPROW row = pixels;
        CHECK(jpeg_write_scanlines(&codec, &row, 1) == 1);
    }
    jpeg_finish_compress(&codec);
    jpeg_destroy_compress(&codec);
    CHECK(fclose(file) == 0);
}

static void test_prefixes(const char *path)
{
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    if (file == NULL)
        return;
    unsigned char bytes[2048];
    size_t length = fread(bytes, 1, sizeof(bytes), file);
    CHECK(length > 0 && length < sizeof(bytes));
    CHECK(fclose(file) == 0);
    for (size_t i = 0; i < length; ++i) {
        file = fopen(path, "wb");
        CHECK(file != NULL);
        if (file == NULL)
            break;
        CHECK(fwrite(bytes, 1, i, file) == i);
        CHECK(fclose(file) == 0);
        photoc_check_result result = photoc_jpeg_check_file(path);
        CHECK(result.status != PHOTOC_CHECK_OK);
        CHECK(photoc_check_code_name(result.code) != NULL);
        CHECK(photoc_check_message(result) != NULL);
    }
}

int main(void)
{
    photoc_check_result result = photoc_jpeg_check_file(NULL);
    CHECK(result.status == PHOTOC_CHECK_ERROR);
    CHECK(result.code == PHOTOC_CHECK_INTERNAL_ERROR &&
          result.system_errno == EINVAL);
    result = photoc_jpeg_check_file("");
    CHECK(result.status == PHOTOC_CHECK_ERROR);
    char directory[] = "photoc-check-core-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    if (failures != 0)
        return EXIT_FAILURE;
    char path[128];
    CHECK(snprintf(path, sizeof(path), "%s/image.jpg", directory) > 0);
    result = photoc_jpeg_check_file(path);
    CHECK(result.code == PHOTOC_CHECK_IO_ERROR &&
          result.system_errno == ENOENT);
    for (unsigned int i = 0; i < 4; ++i) {
        make_jpeg(path, (i & 1) != 0, (i & 2) != 0);
        result = photoc_jpeg_check_file(path);
        CHECK(result.status == PHOTOC_CHECK_OK);
        CHECK(result.code == PHOTOC_CHECK_READABLE);
    }
    test_prefixes(path);
    CHECK(unlink(path) == 0);
    CHECK(rmdir(directory) == 0);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
