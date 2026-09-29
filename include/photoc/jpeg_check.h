#ifndef PHOTOC_JPEG_CHECK_H
#define PHOTOC_JPEG_CHECK_H

#include <stdint.h>

typedef enum {
    PHOTOC_CHECK_OK,
    PHOTOC_CHECK_WARNING,
    PHOTOC_CHECK_ERROR
} photoc_check_status;

typedef enum {
    PHOTOC_CHECK_READABLE,
    PHOTOC_CHECK_EMPTY_FILE,
    PHOTOC_CHECK_INVALID_SIGNATURE,
    PHOTOC_CHECK_INVALID_STRUCTURE,
    PHOTOC_CHECK_TRUNCATED_JPEG,
    PHOTOC_CHECK_DECODE_FAILED,
    PHOTOC_CHECK_JPEG_WARNING,
    PHOTOC_CHECK_STRUCTURE_WARNING,
    PHOTOC_CHECK_MALFORMED_EXIF,
    PHOTOC_CHECK_UNUSUAL_EXIF,
    PHOTOC_CHECK_IO_ERROR,
    PHOTOC_CHECK_RESOURCE_LIMIT,
    PHOTOC_CHECK_UNSUPPORTED_JPEG,
    PHOTOC_CHECK_FILE_CHANGED,
    PHOTOC_CHECK_INTERNAL_ERROR
} photoc_check_code;

typedef struct {
    photoc_check_status status;
    photoc_check_code code;
    int system_errno; /* Meaningful for I/O/internal failures only. */
} photoc_check_result;

#define PHOTOC_CHECK_MAX_FILE_BYTES UINT64_C(536870912)
#define PHOTOC_CHECK_MAX_PIXELS UINT64_C(100000000)
#define PHOTOC_CHECK_MAX_COEFFICIENT_BYTES UINT64_C(268435456)
#define PHOTOC_CHECK_MAX_SCANS 256u

/* Read-only structural, full scanline decode, and EXIF audit of a regular file.
   path is borrowed only for this synchronous call. No caller-owned memory is
   allocated. File bytes and decoded images are not retained in memory: decoder
   output uses one row; multi-scan coefficient storage is explicitly bounded.
   Recoverable codec warnings remain WARNING; operational/resource/unsupported
   errors mean the audit could not finish, not proof of corrupt photographs.
   Only one primary reason is returned, preferring errors and pixel issues
   over metadata warnings. No file, EXIF, ICC, or XMP data is ever rewritten. */
photoc_check_result photoc_jpeg_check_file(const char *path);

/* Stable static strings, borrowed by the caller; never free them. */
const char *photoc_check_status_name(photoc_check_status status);
const char *photoc_check_code_name(photoc_check_code code);
const char *photoc_check_message(photoc_check_result result);

#endif
