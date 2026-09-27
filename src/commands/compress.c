#include "photoc/commands.h"

#include "photoc/exit_codes.h"
#include "photoc/fs.h"
#include "photoc/image.h"
#include "photoc/jpeg_write.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Insert before the JPEG extension so the output remains a .jpg/.jpeg file.
   The caller owns and frees *out. */
static int output_path(const char *source, char **out)
{
    static const char suffix[] = ".compressed";
    size_t length = strlen(source);
    size_t extra = sizeof(suffix) - 1;
    if (length > SIZE_MAX - extra - 1) {
        errno = EOVERFLOW;
        return -1;
    }
    const char *dot = strrchr(source, '.');
    size_t prefix = (size_t)(dot - source);
    char *name = malloc(length + extra + 1);
    if (name == NULL) {
        return -1;
    }
    memcpy(name, source, prefix);
    memcpy(name + prefix, suffix, extra);
    memcpy(name + prefix + extra, dot, length - prefix + 1);
    *out = name;
    return 0;
}

static int image_error(const char *path, photoc_image_result result)
{
    int saved_errno = errno;
    fprintf(stderr, "photoc compress: '%s': %s", path,
            photoc_image_result_message(result));
    if (result == PHOTOC_IMAGE_IO_ERROR) {
        fprintf(stderr, ": %s", strerror(saved_errno));
    }
    fputc('\n', stderr);
    return PHOTOC_EXIT_FAILURE;
}

static int jpeg_error(const char *path, photoc_jpeg_edit_result result)
{
    int saved_errno = errno;
    fprintf(stderr, "photoc compress: '%s': %s", path,
            photoc_jpeg_edit_result_message(result));
    if (result == PHOTOC_JPEG_EDIT_IO_ERROR) {
        fprintf(stderr, ": %s", strerror(saved_errno));
    }
    fputc('\n', stderr);
    return PHOTOC_EXIT_FAILURE;
}

int photoc_command_compress(const char *path, int quality)
{
    photoc_fs_type type;
    if (photoc_fs_get_type(path, &type) != 0) {
        fprintf(stderr, "photoc compress: '%s': %s\n", path, strerror(errno));
        return PHOTOC_EXIT_FAILURE;
    }
    if (type != PHOTOC_FS_FILE || !photoc_fs_is_jpeg(path)) {
        fprintf(stderr, "photoc compress: '%s': expected a regular JPEG file\n",
                path);
        return PHOTOC_EXIT_FAILURE;
    }
    uint64_t original_size;
    if (photoc_fs_file_size(path, &original_size) != 0) {
        fprintf(stderr, "photoc compress: '%s': %s\n", path, strerror(errno));
        return PHOTOC_EXIT_FAILURE;
    }
    char *destination = NULL;
    if (output_path(path, &destination) != 0) {
        fprintf(stderr, "photoc compress: '%s': cannot build output path: %s\n",
                path, strerror(errno));
        return PHOTOC_EXIT_FAILURE;
    }
    bool exists = false;
    if (photoc_fs_exists(destination, &exists) != 0 || exists) {
        int saved_errno = exists ? EEXIST : errno;
        fprintf(stderr, "photoc compress: '%s': %s\n", destination,
                strerror(saved_errno));
        free(destination);
        return PHOTOC_EXIT_FAILURE;
    }

    photoc_jpeg_exif *exif = NULL;
    photoc_jpeg_edit_result exif_result = photoc_jpeg_exif_load_copy(path, &exif);
    if (exif_result != PHOTOC_JPEG_EDIT_OK &&
        exif_result != PHOTOC_JPEG_EDIT_NO_EXIF) {
        int exit_code = jpeg_error(path, exif_result);
        free(destination);
        return exit_code;
    }

    photoc_image image = {0};
    photoc_image_result image_result = photoc_image_decode_jpeg(path, &image);
    if (image_result != PHOTOC_IMAGE_OK) {
        int exit_code = image_error(path, image_result);
        photoc_jpeg_exif_free(exif);
        free(destination);
        return exit_code;
    }
    photoc_jpeg_buffer encoded = {0};
    image_result = photoc_image_encode_jpeg(&image, quality, &encoded);
    photoc_image_cleanup(&image);
    if (image_result != PHOTOC_IMAGE_OK) {
        int exit_code = image_error(path, image_result);
        photoc_jpeg_exif_free(exif);
        free(destination);
        return exit_code;
    }

    photoc_jpeg_edit_result write_result = photoc_jpeg_write_encoded(
        destination, &encoded, exif);
    photoc_jpeg_buffer_cleanup(&encoded);
    photoc_jpeg_exif_free(exif);
    if (write_result != PHOTOC_JPEG_EDIT_OK) {
        int exit_code = jpeg_error(destination, write_result);
        free(destination);
        return exit_code;
    }

    uint64_t compressed_size;
    if (photoc_fs_file_size(destination, &compressed_size) != 0) {
        fprintf(stderr, "photoc compress: '%s': cannot inspect output: %s\n",
                destination, strerror(errno));
        free(destination);
        return PHOTOC_EXIT_FAILURE;
    }
    printf("Output: %s\n", destination);
    printf("Quality: %d\n", quality);
    printf("Original size: %" PRIu64 " bytes\n", original_size);
    printf("Compressed size: %" PRIu64 " bytes\n", compressed_size);
    if (compressed_size <= original_size) {
        printf("Bytes saved: %" PRIu64 " bytes\n",
               original_size - compressed_size);
    } else {
        printf("Bytes saved: -%" PRIu64 " bytes\n",
               compressed_size - original_size);
    }
    printf("Percentage saved: %.2f%%\n",
           (1.0 - (double)compressed_size / (double)original_size) * 100.0);
    free(destination);
    if (ferror(stdout)) {
        fputs("photoc compress: unable to write output\n", stderr);
        return PHOTOC_EXIT_FAILURE;
    }
    return PHOTOC_EXIT_SUCCESS;
}
