#ifndef PHOTOC_FILENAME_TEMPLATE_H
#define PHOTOC_FILENAME_TEMPLATE_H

#include "photoc/photo.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    PHOTOC_TEMPLATE_OK = 0,
    PHOTOC_TEMPLATE_INVALID_ARGUMENT,
    PHOTOC_TEMPLATE_INVALID_TEMPLATE,
    PHOTOC_TEMPLATE_UNKNOWN_PLACEHOLDER,
    PHOTOC_TEMPLATE_MISSING_VALUE,
    PHOTOC_TEMPLATE_NO_MEMORY,
    PHOTOC_TEMPLATE_TOO_LONG
} photoc_template_result;

/* Compiled filename template. Opaque; produced by compile and released with
   photoc_filename_template_free. A compiled template may be rendered many
   times. */
typedef struct photoc_filename_template photoc_filename_template;

/* Parses pattern once into literal chunks and token references, so repeated
   renders skip re-validation, token-name lookup, and literal sanitization.
   On success *out owns a compiled template; on failure *out is NULL.
   error_offset follows photoc_filename_template_validate. */
photoc_template_result photoc_filename_template_compile(
    const char *pattern, photoc_filename_template **out, size_t *error_offset);

/* Renders a compiled template for one photo into a single pre-sized buffer.
   Ownership and error semantics match photoc_filename_template_expand. */
photoc_template_result
photoc_filename_template_render(const photoc_filename_template *compiled,
                                const Photo *photo, uint64_t sequence,
                                unsigned int sequence_width, char **filename,
                                size_t *error_offset);

/* Releases a compiled template. NULL is accepted. */
void photoc_filename_template_free(photoc_filename_template *compiled);

/* Checks placeholder names and brace syntax without requiring a Photo.
   An invalid or unknown placeholder sets error_offset to its opening brace;
   otherwise error_offset is SIZE_MAX. */
photoc_template_result photoc_filename_template_validate(const char *pattern,
                                                         size_t *error_offset);

/* Expands a single filename; no filesystem operation is performed.
   {date} is YYYY-MM-DD and {datetime} is YYYY-MM-DD_HH-MM-SS from the
   capture timestamp. {camera}/{make} use Photo's model/make; {iso},
   {aperture}, and {focal} are bare numbers (no unit or prefix).
   {original} is the source basename without its final extension; {ext} is
   the extension without a dot, preserving the source's exact letter case.
   Sequence width 0 uses no padding; 1..20 set a minimum zero-padded width.
   Missing or invalid metadata required by a placeholder returns
   PHOTOC_TEMPLATE_MISSING_VALUE. Literal {{ and }} produce single braces.
   ASCII controls, DEL, and / \\ : * ? " < > | become underscores in both
   literals and substituted values. An empty result, ".", or ".." is invalid.

   photo and pattern are borrowed. On success *filename is a malloc-owned
   string to free; on failure it is NULL. The output pointer must not already
   own memory. If error_offset is supplied, it receives the opening brace of
   an invalid/unknown/unavailable placeholder, or SIZE_MAX otherwise. */
photoc_template_result
photoc_filename_template_expand(const char *pattern, const Photo *photo,
                                uint64_t sequence, unsigned int sequence_width,
                                char **filename, size_t *error_offset);

/* Static, human-readable result description. Never free it. */
const char *photoc_template_result_message(photoc_template_result result);

#endif
