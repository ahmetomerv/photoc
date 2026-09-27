#include "photoc/commands.h"

#include "photoc/exit_codes.h"
#include "photoc/fs.h"
#include "photoc/photo.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *available(const char *value)
{
    return value == NULL ? "Unavailable" : value;
}

int photoc_command_exif(const char *path)
{
    Photo photo = {0};
    photoc_metadata_result result = photo_load_metadata(path, &photo);
    if (result != PHOTOC_METADATA_OK) {
        int saved_errno = errno;
        fprintf(stderr, "photoc exif: '%s': %s", path,
                photo_metadata_result_message(result));
        if (result == PHOTOC_METADATA_IO_ERROR) {
            fprintf(stderr, ": %s", strerror(saved_errno));
        }
        fputc('\n', stderr);
        return PHOTOC_EXIT_FAILURE;
    }

    char *name = NULL;
    if (photoc_fs_filename(photo.path, &name) != 0) {
        int saved_errno = errno;
        fprintf(stderr, "photoc exif: '%s': unable to extract filename: %s\n",
                path, strerror(saved_errno));
        photo_cleanup(&photo);
        return PHOTOC_EXIT_FAILURE;
    }

    puts("File");
    printf("  Name: %s\n", name);
    printf("  Path: %s\n", photo.path);
    printf("  Size: %" PRIu64 " bytes\n", photo.file_size);
    puts("");

    puts("Image");
    printf("  Dimensions: %" PRIu32 " x %" PRIu32 " pixels\n",
           photo.width, photo.height);
    puts("");

    puts("Camera");
    printf("  Make: %s\n", available(photo.camera_make));
    printf("  Model: %s\n", available(photo.camera_model));
    puts("");

    puts("Exposure");
    if (photo.has_iso) {
        printf("  ISO: %" PRIu32 "\n", photo.iso);
    } else {
        puts("  ISO: Unavailable");
    }
    if (photo.has_aperture) {
        printf("  Aperture: f/%.6g\n", photo.aperture);
    } else {
        puts("  Aperture: Unavailable");
    }
    if (photo.has_exposure_time) {
        printf("  Exposure time: %.6g s\n", photo.exposure_time);
    } else {
        puts("  Exposure time: Unavailable");
    }
    if (photo.has_focal_length) {
        printf("  Focal length: %.6g mm\n", photo.focal_length);
    } else {
        puts("  Focal length: Unavailable");
    }
    puts("");

    puts("Date");
    printf("  Captured: %s\n", available(photo.capture_timestamp));
    puts("");

    puts("Location");
    printf("  GPS: %s\n", photo.has_gps ? "Yes" : "No");
    if (photo.has_gps) {
        printf("  Latitude: %.6f\n", photo.latitude);
        printf("  Longitude: %.6f\n", photo.longitude);
    }

    free(name);
    photo_cleanup(&photo);
    if (ferror(stdout)) {
        fputs("photoc exif: unable to write output\n", stderr);
        return PHOTOC_EXIT_FAILURE;
    }
    return PHOTOC_EXIT_SUCCESS;
}
