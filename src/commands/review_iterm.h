#ifndef PHOTOC_REVIEW_ITERM_H
#define PHOTOC_REVIEW_ITERM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef enum {
    REVIEW_IMAGE_RENDERED,
    REVIEW_IMAGE_UNAVAILABLE,
    REVIEW_IMAGE_OUTPUT_ERROR
} review_image_result;

typedef enum {
    REVIEW_BASE64_OK,
    REVIEW_BASE64_READ_ERROR,
    REVIEW_BASE64_WRITE_ERROR
} review_base64_result;

/* Only direct iTerm sessions are auto-enabled. All strings are borrowed. */
bool review_iterm_auto_supported(const char *term_program,
                                 const char *session_id, const char *term,
                                 const char *tmux, const char *sty);

/* Encode source bytes into destination without buffering the whole file.
   Neither FILE is closed or flushed; both remain owned by the caller. */
review_base64_result review_base64_encode(FILE *source, FILE *destination);

/* Encode an owned or borrowed memory buffer directly into destination. */
review_base64_result review_base64_encode_bytes(const unsigned char *bytes,
                                                size_t length,
                                                FILE *destination);

/* Send an inline JPEG using iTerm2's OSC 1337 File protocol. The path is
   borrowed, and destination remains owned by the caller. Image open/read
   errors return UNAVAILABLE; output errors return OUTPUT_ERROR. */
review_image_result review_iterm_render(FILE *destination, const char *path);

/* The caller owns bytes and must keep them valid until this returns. */
review_image_result review_iterm_render_bytes(FILE *destination,
                                               const unsigned char *bytes,
                                               size_t length);

#endif
