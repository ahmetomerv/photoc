#include "photoc/error.h"

#include "photoc/exit_codes.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const char *photoc_error_kind_name(photoc_error_kind kind)
{
    switch (kind) {
    case PHOTOC_ERR_USAGE:
        return "invalid input";
    case PHOTOC_ERR_UNSUPPORTED:
        return "unsupported file";
    case PHOTOC_ERR_IO:
        return "file I/O error";
    case PHOTOC_ERR_METADATA:
        return "metadata error";
    case PHOTOC_ERR_DECODE:
        return "image decode error";
    case PHOTOC_ERR_COLLISION:
        return "collision";
    case PHOTOC_ERR_INTERNAL:
        return "internal error";
    }
    return "internal error";
}

photoc_error_kind photoc_error_kind_for_metadata(photoc_metadata_result result)
{
    switch (result) {
    case PHOTOC_METADATA_UNSUPPORTED_FORMAT:
        return PHOTOC_ERR_UNSUPPORTED;
    case PHOTOC_METADATA_INVALID_JPEG:
        return PHOTOC_ERR_DECODE;
    case PHOTOC_METADATA_IO_ERROR:
        return PHOTOC_ERR_IO;
    case PHOTOC_METADATA_OK:
    case PHOTOC_METADATA_INVALID_ARGUMENT:
    case PHOTOC_METADATA_NO_MEMORY:
        return PHOTOC_ERR_INTERNAL;
    }
    return PHOTOC_ERR_INTERNAL;
}

photoc_error_kind photoc_error_kind_for_image(photoc_image_result result)
{
    switch (result) {
    case PHOTOC_IMAGE_IO_ERROR:
        return PHOTOC_ERR_IO;
    case PHOTOC_IMAGE_INVALID_JPEG:
    case PHOTOC_IMAGE_TOO_LARGE:
    case PHOTOC_IMAGE_CODEC_ERROR:
        return PHOTOC_ERR_DECODE;
    case PHOTOC_IMAGE_OK:
    case PHOTOC_IMAGE_INVALID_ARGUMENT:
    case PHOTOC_IMAGE_NO_MEMORY:
        return PHOTOC_ERR_INTERNAL;
    }
    return PHOTOC_ERR_INTERNAL;
}

photoc_error_kind
photoc_error_kind_for_jpeg_edit(photoc_jpeg_edit_result result, int err)
{
    switch (result) {
    case PHOTOC_JPEG_EDIT_INVALID_JPEG:
        return PHOTOC_ERR_DECODE;
    case PHOTOC_JPEG_EDIT_NO_EXIF:
    case PHOTOC_JPEG_EDIT_INVALID_EXIF:
    case PHOTOC_JPEG_EDIT_UNSAFE_LAYOUT:
    case PHOTOC_JPEG_EDIT_EXIF_TOO_LARGE:
    case PHOTOC_JPEG_EDIT_INVALID_ICC:
    case PHOTOC_JPEG_EDIT_METADATA_TOO_LARGE:
    case PHOTOC_JPEG_EDIT_UNSAFE_METADATA_LAYOUT:
        return PHOTOC_ERR_METADATA;
    case PHOTOC_JPEG_EDIT_UNSAFE_SOURCE:
        return PHOTOC_ERR_UNSUPPORTED;
    case PHOTOC_JPEG_EDIT_IO_ERROR:
        return err == EEXIST ? PHOTOC_ERR_COLLISION : PHOTOC_ERR_IO;
    case PHOTOC_JPEG_EDIT_OK:
    case PHOTOC_JPEG_EDIT_INVALID_ARGUMENT:
    case PHOTOC_JPEG_EDIT_NO_MEMORY:
        return PHOTOC_ERR_INTERNAL;
    }
    return PHOTOC_ERR_INTERNAL;
}

photoc_error_kind photoc_error_kind_for_template(photoc_template_result result)
{
    switch (result) {
    case PHOTOC_TEMPLATE_MISSING_VALUE:
        return PHOTOC_ERR_METADATA;
    case PHOTOC_TEMPLATE_TOO_LONG:
        return PHOTOC_ERR_IO;
    case PHOTOC_TEMPLATE_OK:
    case PHOTOC_TEMPLATE_INVALID_ARGUMENT:
    case PHOTOC_TEMPLATE_INVALID_TEMPLATE:
    case PHOTOC_TEMPLATE_UNKNOWN_PLACEHOLDER:
    case PHOTOC_TEMPLATE_NO_MEMORY:
        return PHOTOC_ERR_INTERNAL;
    }
    return PHOTOC_ERR_INTERNAL;
}

static int report_exit(photoc_error_note note, photoc_error_kind kind)
{
    if (note == PHOTOC_ERR_NOTE_WARNING) {
        return 0;
    }
    if (kind == PHOTOC_ERR_USAGE) {
        return PHOTOC_EXIT_USAGE;
    }
    return PHOTOC_EXIT_FAILURE;
}

static void write_report(const char *command, photoc_error_note note,
                         photoc_error_kind kind, const char *path,
                         const char *detail, int system_errno)
{
    if (kind == PHOTOC_ERR_IO && system_errno == EEXIST) {
        kind = PHOTOC_ERR_COLLISION;
        detail = "output already exists";
        system_errno = 0;
    }
    if (detail == NULL || detail[0] == '\0') {
        detail = "unexpected failure";
    }

    fprintf(stderr, "photoc%s%s: ", command == NULL ? "" : " ",
            command == NULL ? "" : command);
    if (note == PHOTOC_ERR_NOTE_WARNING) {
        fputs("warning: ", stderr);
    }
    if (path != NULL) {
        fprintf(stderr, "'%s': ", path);
    }
    if (note == PHOTOC_ERR_NOTE_SKIPPED) {
        fputs("skipped: ", stderr);
    }
    fprintf(stderr, "%s: %s", photoc_error_kind_name(kind), detail);
    if (system_errno != 0) {
        fprintf(stderr, ": %s", strerror(system_errno));
    }
    fputc('\n', stderr);
}

int photoc_error_report(const char *command, photoc_error_note note,
                        photoc_error_kind kind, const char *path,
                        const char *detail, int system_errno)
{
    write_report(command, note, kind, path, detail, system_errno);
    return report_exit(note, kind);
}

int photoc_error_reportf(const char *command, photoc_error_note note,
                         photoc_error_kind kind, const char *path,
                         int system_errno, const char *format, ...)
{
    char detail[512];
    va_list args;
    va_start(args, format);
    vsnprintf(detail, sizeof(detail), format, args);
    va_end(args);
    detail[sizeof(detail) - 1] = '\0';
    return photoc_error_report(command, note, kind, path, detail, system_errno);
}

int photoc_error_metadata(const char *command, photoc_error_note note,
                          const char *path, photoc_metadata_result result,
                          int system_errno)
{
    const char *detail = photo_metadata_result_message(result);
    int err = result == PHOTOC_METADATA_IO_ERROR ? system_errno : 0;
    return photoc_error_report(command, note,
                               photoc_error_kind_for_metadata(result), path,
                               detail, err);
}

int photoc_error_image(const char *command, photoc_error_note note,
                       const char *path, photoc_image_result result,
                       int system_errno)
{
    const char *detail = photoc_image_result_message(result);
    int err = result == PHOTOC_IMAGE_IO_ERROR ? system_errno : 0;
    return photoc_error_report(
        command, note, photoc_error_kind_for_image(result), path, detail, err);
}

int photoc_error_jpeg_edit(const char *command, photoc_error_note note,
                           const char *path, photoc_jpeg_edit_result result,
                           int system_errno)
{
    photoc_error_kind kind =
        photoc_error_kind_for_jpeg_edit(result, system_errno);
    const char *detail = photoc_jpeg_edit_result_message(result);
    int err = 0;
    if (kind == PHOTOC_ERR_COLLISION) {
        detail = "output already exists";
    } else if (kind == PHOTOC_ERR_IO) {
        err = system_errno;
    }
    return photoc_error_report(command, note, kind, path, detail, err);
}
