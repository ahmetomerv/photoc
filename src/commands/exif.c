#include "photoc/commands.h"

#include "photoc/error.h"
#include "photoc/exit_codes.h"
#include "photoc/fs.h"
#include "photoc/json.h"
#include "photoc/photo.h"
#include "photoc/size.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *available(const char *value)
{
    return value == NULL ? "Unavailable" : value;
}

static void print_human(const Photo *photo, const char *name)
{
    char size[PHOTOC_SIZE_TEXT_CAPACITY];
    photoc_size_format((double)photo->file_size, size);
    puts("File");
    printf("  Name: %s\n", name);
    printf("  Path: %s\n", photo->path);
    printf("  Size: %s\n", size);
    puts("");

    puts("Image");
    if (photo->has_width && photo->has_height)
        printf("  Dimensions: %" PRIu32 " x %" PRIu32 " pixels\n", photo->width,
               photo->height);
    else
        puts("  Dimensions: Unavailable");
    if (photo->format == PHOTOC_FORMAT_SONY_ARW || photo->has_orientation) {
        if (photo->has_orientation)
            printf("  Orientation: %u (EXIF/TIFF; pixels unchanged)\n",
                   (unsigned int)photo->orientation);
        else
            puts("  Orientation: Unavailable");
    }
    puts("");

    puts("Camera");
    printf("  Make: %s\n", available(photo->camera_make));
    printf("  Model: %s\n", available(photo->camera_model));
    puts("");

    puts("Exposure");
    if (photo->has_iso) {
        printf("  ISO: %" PRIu32 "\n", photo->iso);
    } else {
        puts("  ISO: Unavailable");
    }
    if (photo->has_aperture) {
        printf("  Aperture: f/%.6g\n", photo->aperture);
    } else {
        puts("  Aperture: Unavailable");
    }
    if (photo->has_exposure_time) {
        printf("  Exposure time: %.6g s\n", photo->exposure_time);
    } else {
        puts("  Exposure time: Unavailable");
    }
    if (photo->has_focal_length) {
        printf("  Focal length: %.6g mm\n", photo->focal_length);
    } else {
        puts("  Focal length: Unavailable");
    }
    puts("");

    puts("Date");
    printf("  Captured: %s\n", available(photo->capture_timestamp));
    puts("");

    puts("Location");
    printf("  GPS: %s\n", photo->has_gps ? "Yes" : "No");
    if (photo->has_gps) {
        printf("  Latitude: %.6f\n", photo->latitude);
        printf("  Longitude: %.6f\n", photo->longitude);
    }
}

static void print_json_number(bool present, double value)
{
    if (present && isfinite(value)) {
        printf("%.15g", value);
    } else {
        fputs("null", stdout);
    }
}

static void print_json(const Photo *photo, const char *name)
{
    fputs("{\n  \"file\": {\n    \"name\": ", stdout);
    photoc_json_write_string(stdout, name);
    fputs(",\n    \"path\": ", stdout);
    photoc_json_write_string(stdout, photo->path);
    printf(",\n    \"size_bytes\": %" PRIu64 ",\n    \"format\": ",
           photo->file_size);
    photoc_json_write_string(stdout, photoc_format_name(photo->format));
    fputs("\n  },\n", stdout);

    fputs("  \"image\": {\n    \"width\": ", stdout);
    if (photo->has_width)
        printf("%" PRIu32, photo->width);
    else
        fputs("null", stdout);
    fputs(",\n    \"height\": ", stdout);
    if (photo->has_height)
        printf("%" PRIu32, photo->height);
    else
        fputs("null", stdout);
    fputs(",\n    \"orientation\": ", stdout);
    if (photo->has_orientation)
        printf("%u", (unsigned int)photo->orientation);
    else
        fputs("null", stdout);
    fputs("\n  },\n", stdout);

    fputs("  \"camera\": {\n    \"make\": ", stdout);
    photoc_json_write_string(stdout, photo->camera_make);
    fputs(",\n    \"model\": ", stdout);
    photoc_json_write_string(stdout, photo->camera_model);
    fputs("\n  },\n", stdout);

    fputs("  \"exposure\": {\n    \"iso\": ", stdout);
    if (photo->has_iso) {
        printf("%" PRIu32, photo->iso);
    } else {
        fputs("null", stdout);
    }
    fputs(",\n    \"aperture\": ", stdout);
    print_json_number(photo->has_aperture, photo->aperture);
    fputs(",\n    \"exposure_time_seconds\": ", stdout);
    print_json_number(photo->has_exposure_time, photo->exposure_time);
    fputs(",\n    \"focal_length_mm\": ", stdout);
    print_json_number(photo->has_focal_length, photo->focal_length);
    fputs("\n  },\n", stdout);

    fputs("  \"date\": {\n    \"captured\": ", stdout);
    photoc_json_write_string(stdout, photo->capture_timestamp);
    fputs("\n  },\n", stdout);

    fputs("  \"location\": {\n    \"has_gps\": ", stdout);
    fputs(photo->has_gps ? "true" : "false", stdout);
    fputs(",\n    \"latitude\": ", stdout);
    print_json_number(photo->has_gps, photo->latitude);
    fputs(",\n    \"longitude\": ", stdout);
    print_json_number(photo->has_gps, photo->longitude);
    fputs("\n  }\n}\n", stdout);
}

int photoc_command_exif_with_output(const char *path, bool json,
                                    const photoc_output *output)
{
    photoc_output_verbose(output, "exif",
                          "input: %s; mode: metadata; output: %s\n", path,
                          json ? "JSON" : "human");
    Photo photo = {0};
    photoc_metadata_result result = photo_load_metadata(path, &photo);
    photoc_output_verbose(output, "exif",
                          "files inspected: 1; metadata parse failures: %u\n",
                          result == PHOTOC_METADATA_OK ? 0u : 1u);
    if (result != PHOTOC_METADATA_OK) {
        int saved_errno = errno;
        return photoc_error_metadata("exif", PHOTOC_ERR_NOTE_NONE, path, result,
                                     saved_errno);
    }

    char *name = NULL;
    if (photoc_fs_filename(photo.path, &name) != 0) {
        int saved_errno = errno;
        photo_cleanup(&photo);
        return photoc_error_report("exif", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_INTERNAL, path,
                                   "unable to extract filename", saved_errno);
    }

    if (json) {
        print_json(&photo, name);
    } else {
        print_human(&photo, name);
    }

    free(name);
    photo_cleanup(&photo);
    if (ferror(stdout)) {
        return photoc_error_report("exif", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                   NULL, "unable to write output", EIO);
    }
    return PHOTOC_EXIT_SUCCESS;
}

int photoc_command_exif(const char *path, bool json)
{
    return photoc_command_exif_with_output(path, json, NULL);
}
