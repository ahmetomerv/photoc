#ifndef PHOTOC_ERROR_H
#define PHOTOC_ERROR_H

#include "photoc/filename_template.h"
#include "photoc/image.h"
#include "photoc/jpeg_write.h"
#include "photoc/photo.h"

/* User-facing failure classes. Usage (invalid input) stays exit status 2.
   Every other class is exit status 1. Messages name the class so a raw
   system or library string is never the only text. */
typedef enum {
    PHOTOC_ERR_USAGE,
    PHOTOC_ERR_UNSUPPORTED,
    PHOTOC_ERR_IO,
    PHOTOC_ERR_METADATA,
    PHOTOC_ERR_DECODE,
    PHOTOC_ERR_COLLISION,
    PHOTOC_ERR_INTERNAL
} photoc_error_kind;

typedef enum {
    PHOTOC_ERR_NOTE_NONE = 0,
    PHOTOC_ERR_NOTE_WARNING,
    PHOTOC_ERR_NOTE_SKIPPED
} photoc_error_note;

/* Static label such as "file I/O error". Never free it. */
const char *photoc_error_kind_name(photoc_error_kind kind);

photoc_error_kind photoc_error_kind_for_metadata(photoc_metadata_result result);
photoc_error_kind photoc_error_kind_for_image(photoc_image_result result);
/* err is the errno saved for PHOTOC_JPEG_EDIT_IO_ERROR; EEXIST is a collision. */
photoc_error_kind
photoc_error_kind_for_jpeg_edit(photoc_jpeg_edit_result result, int err);
photoc_error_kind photoc_error_kind_for_template(photoc_template_result result);

/* Writes one line to stderr:
     photoc <command>: [warning: ]['<path>': ][skipped: ]<kind>: <detail>[: strerror]
   path may be NULL. system_errno 0 omits the system text. An I/O error with
   EEXIST is reported as a collision, "output already exists", without strerror.
   Returns 0 for warnings, PHOTOC_EXIT_USAGE for invalid input, otherwise
   PHOTOC_EXIT_FAILURE. detail is borrowed and must not be NULL. */
int photoc_error_report(const char *command, photoc_error_note note,
                        photoc_error_kind kind, const char *path,
                        const char *detail, int system_errno);

int photoc_error_reportf(const char *command, photoc_error_note note,
                         photoc_error_kind kind, const char *path,
                         int system_errno, const char *format, ...);

int photoc_error_metadata(const char *command, photoc_error_note note,
                          const char *path, photoc_metadata_result result,
                          int system_errno);
int photoc_error_image(const char *command, photoc_error_note note,
                       const char *path, photoc_image_result result,
                       int system_errno);
int photoc_error_jpeg_edit(const char *command, photoc_error_note note,
                           const char *path, photoc_jpeg_edit_result result,
                           int system_errno);

#endif
