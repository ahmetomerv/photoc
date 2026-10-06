#define _POSIX_C_SOURCE 200809L

#include "review_display.h"

#include "review_terminal.h"

#include "photoc/sharpness.h"

#include <math.h>

photoc_image_result review_score_get(review_score_cache *cache,
                                     const char *path)
{
    if (!cache->attempted) {
        cache->result = photoc_sharpness_score_jpeg(
            path, PHOTOC_SHARPNESS_DEFAULT_MAX_DIMENSION, &cache->score);
        cache->attempted = true;
    }
    return cache->result;
}

void review_score_fail(review_score_cache *cache, photoc_image_result reason)
{
    if (!cache->attempted) {
        cache->result = reason;
        cache->attempted = true;
    }
}

static bool has_text(const char *text)
{
    return text != NULL && text[0] != '\0';
}

static void print_camera(FILE *stream, const Photo *photo)
{
    if (has_text(photo->camera_make)) {
        review_terminal_print_safe(stream, photo->camera_make);
        if (has_text(photo->camera_model))
            fputc(' ', stream);
    }
    if (has_text(photo->camera_model))
        review_terminal_print_safe(stream, photo->camera_model);
}

void review_display_metadata(FILE *stream, const Photo *photo, bool details)
{
    bool camera = has_text(photo->camera_make) || has_text(photo->camera_model);
    bool dimensions = photo->has_width && photo->width > 0 &&
                      photo->has_height && photo->height > 0;
    bool exposure = photo->has_exposure_time &&
                    isfinite(photo->exposure_time) && photo->exposure_time > 0;
    bool aperture =
        photo->has_aperture && isfinite(photo->aperture) && photo->aperture > 0;
    bool iso = photo->has_iso && photo->iso > 0;
    bool focal = photo->has_focal_length && isfinite(photo->focal_length) &&
                 photo->focal_length > 0;
    bool captured = has_text(photo->capture_timestamp);
    bool lens = details && has_text(photo->lens_model);

    if (!camera && !dimensions && !exposure && !aperture && !iso && !focal &&
        !captured && !lens) {
        fputs("Metadata unavailable\n", stream);
        return;
    }
    if (details)
        fputs("Details\n", stream);
    if (camera) {
        if (details)
            fputs("Camera: ", stream);
        print_camera(stream, photo);
        fputc('\n', stream);
    }
    if (dimensions)
        fprintf(stream, details ? "Dimensions: %u x %u\n" : "%u x %u\n",
                photo->width, photo->height);
    if (exposure || aperture || iso || focal) {
        if (details)
            fputs("Exposure: ", stream);
        bool separator = false;
        if (exposure) {
            if (photo->exposure_time < 1.0)
                fprintf(stream, "1/%.0fs", 1.0 / photo->exposure_time);
            else
                fprintf(stream, "%.2fs", photo->exposure_time);
            separator = true;
        }
        if (aperture) {
            fprintf(stream, "%sf/%.1f", separator ? "  " : "", photo->aperture);
            separator = true;
        }
        if (iso) {
            fprintf(stream, "%sISO %u", separator ? "  " : "", photo->iso);
            separator = true;
        }
        if (focal)
            fprintf(stream, "%s%.0fmm", separator ? "  " : "",
                    photo->focal_length);
        fputc('\n', stream);
    }
    if (captured) {
        if (details)
            fputs("Captured: ", stream);
        review_terminal_print_safe(stream, photo->capture_timestamp);
        fputc('\n', stream);
    }
    if (lens) {
        fputs("Lens: ", stream);
        review_terminal_print_safe(stream, photo->lens_model);
        fputc('\n', stream);
    }
}

void review_display_sharpness(FILE *stream, const review_score_cache *cache)
{
    if (cache->attempted && cache->result == PHOTOC_IMAGE_OK)
        fprintf(stream, "Sharpness: %.3f\n", cache->score);
    else
        fputs("Sharpness: unavailable\n", stream);
}
