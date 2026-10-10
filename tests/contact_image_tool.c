#include "photoc/image.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int bounds_ok(uint32_t width, uint32_t height)
{
    return width != 0 && height != 0 && width <= 8192 && height <= 8192 &&
           (uint64_t)width * height <= 4194304u;
}

static void store_pixel(photoc_image *image, uint32_t x, uint32_t y,
                        unsigned char r, unsigned char g, unsigned char b)
{
    unsigned char *pixel =
        image->pixels + (size_t)y * image->stride + (size_t)x * 3;
    pixel[0] = r;
    pixel[1] = g;
    pixel[2] = b;
}

static int write_encoded(const char *path, photoc_image *image)
{
    photoc_jpeg_buffer encoded = {0};
    int result = 1;
    if (photoc_image_encode_jpeg(image, 100, &encoded) == PHOTOC_IMAGE_OK) {
        FILE *file = fopen(path, "wb");
        if (file != NULL) {
            size_t written = fwrite(encoded.data, 1, encoded.size, file);
            int close_result = fclose(file);
            result = written == encoded.size && close_result == 0 ? 0 : 1;
        }
    }
    photoc_jpeg_buffer_cleanup(&encoded);
    return result;
}

static int make_image(const char *path, uint32_t width, uint32_t height)
{
    if (!bounds_ok(width, height))
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
            store_pixel(&image, x, y, x < width / 2 ? 245 : 8, 8,
                        x < width / 2 ? 8 : 245);
        }
    }
    int result = write_encoded(path, &image);
    photoc_image_cleanup(&image);
    return result;
}

/* A 2x2 colour card with a centred black/white checkerboard. The asymmetric
   quadrant colours expose orientation, scaling and cropping while the checker
   keeps high-frequency detail that an over-coarse decode would blur away. */
static int make_card(const char *path, uint32_t width, uint32_t height)
{
    if (!bounds_ok(width, height))
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
            int left = x < width / 2;
            int top = y < height / 2;
            if (top)
                store_pixel(&image, x, y, left ? 220 : 30, left ? 30 : 190,
                            left ? 30 : 30);
            else
                store_pixel(&image, x, y, left ? 30 : 225, left ? 30 : 205,
                            left ? 215 : 30);
        }
    }
    uint32_t side = (width < height ? width : height) / 2;
    uint32_t origin_x = (width - side) / 2;
    uint32_t origin_y = (height - side) / 2;
    uint32_t cell = side / 16;
    if (cell < 2)
        cell = 2;
    for (uint32_t y = 0; y < side; ++y) {
        for (uint32_t x = 0; x < side; ++x) {
            unsigned char value = ((x / cell + y / cell) & 1u) != 0 ? 255 : 0;
            store_pixel(&image, origin_x + x, origin_y + y, value, value,
                        value);
        }
    }
    int result = write_encoded(path, &image);
    photoc_image_cleanup(&image);
    return result;
}

/* A horizontal blue-to-red ramp. Sampling it left to right catches a flipped,
   stretched or cropped thumbnail without relying on exact pixel values. */
static int make_gradient(const char *path, uint32_t width, uint32_t height)
{
    if (!bounds_ok(width, height))
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
            unsigned char red =
                (unsigned char)(width > 1 ? (uint64_t)x * 255 / (width - 1)
                                          : 0);
            store_pixel(&image, x, y, red, 0, (unsigned char)(255 - red));
        }
    }
    int result = write_encoded(path, &image);
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

/* Luma min/max/mean plus dark and bright counts over a rectangle. The range
   shows whether fine detail survived; the counts show whether the region is
   blank or one-sided. */
static int stats_image(const char *path, uint32_t x, uint32_t y,
                       uint32_t region_width, uint32_t region_height)
{
    photoc_image image = {0};
    if (photoc_image_decode_jpeg(path, &image) != PHOTOC_IMAGE_OK)
        return 1;
    int result = 1;
    if (region_width != 0 && region_height != 0 && x < image.width &&
        y < image.height && region_width <= image.width - x &&
        region_height <= image.height - y) {
        uint64_t sum = 0;
        uint64_t count = (uint64_t)region_width * region_height;
        unsigned int minimum = 255;
        unsigned int maximum = 0;
        uint32_t dark = 0;
        uint32_t bright = 0;
        for (uint32_t row = y; row < y + region_height; ++row) {
            for (uint32_t col = x; col < x + region_width; ++col) {
                const unsigned char *pixel =
                    image.pixels + (size_t)row * image.stride + (size_t)col * 3;
                unsigned int luma =
                    ((unsigned int)pixel[0] + pixel[1] + pixel[2]) / 3u;
                if (luma < minimum)
                    minimum = luma;
                if (luma > maximum)
                    maximum = luma;
                sum += luma;
                if (luma < 64)
                    ++dark;
                if (luma > 192)
                    ++bright;
            }
        }
        printf("%u %u %.1f %u %u\n", minimum, maximum,
               (double)sum / (double)count, dark, bright);
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
    if (argc == 5 && strcmp(argv[1], "make-card") == 0)
        return make_card(argv[2], (uint32_t)strtoul(argv[3], NULL, 10),
                         (uint32_t)strtoul(argv[4], NULL, 10));
    if (argc == 5 && strcmp(argv[1], "make-gradient") == 0)
        return make_gradient(argv[2], (uint32_t)strtoul(argv[3], NULL, 10),
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
    if (argc == 7 && strcmp(argv[1], "stats") == 0)
        return stats_image(argv[2], (uint32_t)strtoul(argv[3], NULL, 10),
                           (uint32_t)strtoul(argv[4], NULL, 10),
                           (uint32_t)strtoul(argv[5], NULL, 10),
                           (uint32_t)strtoul(argv[6], NULL, 10));
    return 2;
}
