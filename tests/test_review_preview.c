#define _POSIX_C_SOURCE 200809L

#include "review_preview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static int make_jpeg(const char *path, unsigned int seed)
{
    photoc_image image = {.width = 2400, .height = 1600,
                          .stride = 2400u * 3u,
                          .pixel_bytes = 2400u * 1600u * 3u};
    image.pixels = malloc(image.pixel_bytes);
    if (image.pixels == NULL)
        return -1;
    for (size_t i = 0; i < image.pixel_bytes; ++i) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        image.pixels[i] = (unsigned char)seed;
    }
    photoc_jpeg_buffer jpeg = {0};
    int result = -1;
    if (photoc_image_encode_jpeg(&image, 90, &jpeg) == PHOTOC_IMAGE_OK) {
        FILE *file = fopen(path, "wb");
        if (file != NULL) {
            bool wrote = fwrite(jpeg.data, 1, jpeg.size, file) == jpeg.size;
            int closed = fclose(file);
            if (wrote && closed == 0)
                result = 0;
        }
    }
    photoc_jpeg_buffer_cleanup(&jpeg);
    photoc_image_cleanup(&image);
    return result;
}

int main(void)
{
    char path[] = "/tmp/photoc-review-preview-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd < 0)
        return 1;
    CHECK(close(fd) == 0);
    if (make_jpeg(path, 123456u) != 0) {
        unlink(path);
        return 1;
    }
    struct stat before;
    if (stat(path, &before) != 0) {
        unlink(path);
        return 1;
    }
    CHECK(before.st_size > 512 * 1024);

    review_preview_cache cache = {0};
    Photo photo = {.has_orientation = true, .orientation = 6};
    const photoc_jpeg_buffer *preview = review_preview_get(&cache, 0, path,
                                                            &photo);
    CHECK(preview != NULL);
    if (preview != NULL) {
        CHECK(preview->size * 5 <= (size_t)before.st_size * 4);
        CHECK(cache.bytes == preview->size);
        CHECK(review_preview_get(&cache, 0, path, &photo) == preview);
        char output[] = "/tmp/photoc-review-oriented-XXXXXX";
        int output_fd = mkstemp(output);
        CHECK(output_fd >= 0);
        if (output_fd >= 0) {
            FILE *file = fdopen(output_fd, "wb");
            CHECK(file != NULL);
            if (file != NULL) {
                CHECK(fwrite(preview->data, 1, preview->size, file) ==
                      preview->size);
                CHECK(fclose(file) == 0);
                uint32_t width = 0, height = 0;
                CHECK(photoc_image_jpeg_dimensions(output, &width, &height) ==
                      PHOTOC_IMAGE_OK);
                CHECK(width < height && height <= 1600);
            } else {
                close(output_fd);
            }
            CHECK(unlink(output) == 0);
        }
    }
    for (size_t index = 1; index <= REVIEW_PREVIEW_SLOTS; ++index)
        CHECK(review_preview_get(&cache, index, path, &photo) != NULL);
    CHECK(cache.bytes <= 8u * 1024u * 1024u);
    CHECK(make_jpeg(path, 654321u) == 0);
    CHECK(review_preview_get(&cache, REVIEW_PREVIEW_SLOTS, path, &photo) !=
          NULL);
    struct stat after = {0};
    CHECK(stat(path, &after) == 0);
    CHECK(after.st_size > 512 * 1024);
    CHECK(unlink(path) == 0);
    CHECK(review_preview_get(&cache, 0, path, &photo) == NULL);
    review_preview_cleanup(&cache);
    CHECK(cache.bytes == 0);
    return failures == 0 ? 0 : 1;
}
