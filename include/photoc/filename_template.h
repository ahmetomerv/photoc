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
photoc_template_result photoc_filename_template_expand(
    const char *pattern, const Photo *photo, uint64_t sequence,
    unsigned int sequence_width, char **filename, size_t *error_offset);

/* Static, human-readable result description. Never free it. */
const char *photoc_template_result_message(photoc_template_result result);

#endif
