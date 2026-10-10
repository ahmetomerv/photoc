#include "compress_probe.h"
#include "photoc/quality_search.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

int main(void)
{
    photoc_image image = {0};
    CHECK(photoc_image_decode_jpeg(PHOTOC_COMPRESS_PROBE_FIXTURE, &image) ==
          PHOTOC_IMAGE_OK);
    if (image.pixels == NULL) {
        return 1;
    }

    photoc_jpeg_buffer quality_60 = {0};
    CHECK(photoc_image_encode_jpeg(&image, 60, &quality_60) == PHOTOC_IMAGE_OK);
    if (quality_60.data != NULL) {
        photoc_compress_probe probe = {.image = &image,
                                       .target_bytes = quality_60.size,
                                       .error = PHOTOC_IMAGE_OK};
        photoc_quality_choice choice = {0};
        CHECK(photoc_quality_search(probe.target_bytes, 20,
                                    photoc_compress_probe_size, &probe,
                                    &choice) == 0);
        CHECK(choice.target_met);
        CHECK(choice.quality >= 60 && choice.quality < 100);
        CHECK(probe.retained.data != NULL);
        CHECK(probe.retained_quality == choice.quality);
        CHECK(probe.peak_probe_buffers == 2);

        photoc_jpeg_buffer chosen = {0};
        CHECK(photoc_compress_probe_take(&probe, choice.quality, &chosen));
        CHECK(probe.retained.data == NULL);
        photoc_jpeg_buffer direct = {0};
        CHECK(photoc_image_encode_jpeg(&image, choice.quality, &direct) ==
              PHOTOC_IMAGE_OK);
        CHECK(chosen.size == direct.size);
        if (chosen.data != NULL && direct.data != NULL &&
            chosen.size == direct.size) {
            CHECK(memcmp(chosen.data, direct.data, chosen.size) == 0);
        }
        photoc_jpeg_buffer_cleanup(&chosen);
        photoc_jpeg_buffer_cleanup(&direct);
        photoc_compress_probe_clear(&probe);

        probe = (photoc_compress_probe){
            .image = &image, .target_bytes = 1, .error = PHOTOC_IMAGE_OK};
        CHECK(photoc_quality_search(1, 70, photoc_compress_probe_size, &probe,
                                    &choice) == 0);
        CHECK(!choice.target_met);
        CHECK(choice.quality == 70);
        CHECK(probe.retained_quality == 70);
        CHECK(probe.peak_probe_buffers == 1);
        photoc_compress_probe_clear(&probe);
        CHECK(probe.retained.data == NULL);

        probe = (photoc_compress_probe){.image = &image,
                                        .target_bytes = quality_60.size,
                                        .error = PHOTOC_IMAGE_OK};
        uint64_t probe_size = 0;
        CHECK(photoc_compress_probe_size(20, &probe_size, &probe) == 0);
        CHECK(probe.retained.data != NULL);
        probe.metadata_overhead = UINT64_MAX;
        errno = 0;
        CHECK(photoc_compress_probe_size(100, &probe_size, &probe) == -1);
        CHECK(errno == EOVERFLOW);
        CHECK(probe.retained.data != NULL);
        photoc_compress_probe_clear(&probe);
        CHECK(probe.retained.data == NULL);
    }
    photoc_jpeg_buffer_cleanup(&quality_60);
    photoc_image_cleanup(&image);
    return failures == 0 ? 0 : 1;
}
