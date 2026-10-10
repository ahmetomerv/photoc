#include "photoc/image.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int make_image(const char *path, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || width > 8192 || height > 8192 ||
        (uint64_t)width * height > 4194304u)
        return 1;
    photoc_image image = {.width = width,
                          .height = height,
                          .stride = (size_t)width * 3,
                          .pixel_bytes = (size_t)width * height * 3};
    image.pixels = malloc(image.pixel_bytes);
    if (image.pixels == NULL)
        return 1;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            unsigned char *pixel =
                image.pixels + (size_t)y * image.stride + (size_t)x * 3;
            pixel[0] = x < width / 2 ? 245 : 8;
            pixel[1] = 8;
            pixel[2] = x < width / 2 ? 8 : 245;
        }
    }
    photoc_jpeg_buffer encoded = {0};
    int result = 1;
    if (photoc_image_encode_jpeg(&image, 100, &encoded) == PHOTOC_IMAGE_OK) {
        FILE *file = fopen(path, "wb");
        if (file != NULL) {
            size_t written = fwrite(encoded.data, 1, encoded.size, file);
            int close_result = fclose(file);
            result = written == encoded.size && close_result == 0 ? 0 : 1;
        }
    }
    photoc_jpeg_buffer_cleanup(&encoded);
    photoc_image_cleanup(&image);
    return result;
}

static int inspect_image(const char *path, const char *mode, uint32_t x,
                         uint32_t y, uint32_t region_width,
                         uint32_t region_height)
{
    photoc_image image = {0};
    if (photoc_image_decode_jpeg(path, &image) != PHOTOC_IMAGE_OK)
        return 1;
    int result = 1;
    if (x < image.width && y < image.height &&
        region_width <= image.width - x && region_height <= image.height - y) {
        if (strcmp(mode, "probe") == 0) {
            const unsigned char *pixel =
                image.pixels + (size_t)y * image.stride + (size_t)x * 3;
            printf("%u %u %u %u %u\n", image.width, image.height, pixel[0],
                   pixel[1], pixel[2]);
        } else {
            uint32_t dark = 0;
            for (uint32_t row = y; row < y + region_height; ++row) {
                for (uint32_t col = x; col < x + region_width; ++col) {
                    const unsigned char *pixel = image.pixels +
                                                 (size_t)row * image.stride +
                                                 (size_t)col * 3;
                    if ((unsigned int)pixel[0] + pixel[1] + pixel[2] < 300u)
                        ++dark;
                }
            }
            printf("%u\n", dark);
        }
        result = 0;
    }
    photoc_image_cleanup(&image);
    return result;
}

int main(int argc, char **argv)
{
    if (argc == 5 && strcmp(argv[1], "make") == 0)
        return make_image(argv[2], (uint32_t)strtoul(argv[3], NULL, 10),
                          (uint32_t)strtoul(argv[4], NULL, 10));
    if (argc == 5 && strcmp(argv[1], "probe") == 0)
        return inspect_image(argv[2], argv[1],
                             (uint32_t)strtoul(argv[3], NULL, 10),
                             (uint32_t)strtoul(argv[4], NULL, 10), 1, 1);
    if (argc == 7 && strcmp(argv[1], "dark") == 0)
        return inspect_image(argv[2], argv[1],
                             (uint32_t)strtoul(argv[3], NULL, 10),
                             (uint32_t)strtoul(argv[4], NULL, 10),
                             (uint32_t)strtoul(argv[5], NULL, 10),
                             (uint32_t)strtoul(argv[6], NULL, 10));
    return 2;
}
