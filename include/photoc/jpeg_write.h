#ifndef PHOTOC_JPEG_WRITE_H
#define PHOTOC_JPEG_WRITE_H

#include <stdbool.h>
#include <stdint.h>

#include "photoc/image.h"

typedef struct photoc_jpeg_exif photoc_jpeg_exif;

typedef enum {
    PHOTOC_JPEG_EDIT_OK = 0,
    PHOTOC_JPEG_EDIT_INVALID_ARGUMENT,
    PHOTOC_JPEG_EDIT_INVALID_JPEG,
    PHOTOC_JPEG_EDIT_NO_EXIF,
    PHOTOC_JPEG_EDIT_INVALID_EXIF,
    PHOTOC_JPEG_EDIT_UNSAFE_LAYOUT,
    PHOTOC_JPEG_EDIT_EXIF_TOO_LARGE,
    PHOTOC_JPEG_EDIT_IO_ERROR,
    PHOTOC_JPEG_EDIT_NO_MEMORY,
    PHOTOC_JPEG_EDIT_UNSAFE_SOURCE
} photoc_jpeg_edit_result;

/* Load an owned, editable copy of the EXIF from a JPEG. A JPEG without an
   EXIF APP1 segment returns NO_EXIF. The output pointer must not already own
   memory; on failure *out is NULL. Free with photoc_jpeg_exif_free. */
photoc_jpeg_edit_result photoc_jpeg_exif_load_copy(const char *source_path,
                                                   photoc_jpeg_exif **out);

/* Deep-copy an edited EXIF document. The output follows the same ownership
   rules as load_copy. The source remains independent and unchanged. */
photoc_jpeg_edit_result photoc_jpeg_exif_copy(const photoc_jpeg_exif *source,
                                              photoc_jpeg_exif **out);

/* True when the EXIF GPS IFD has entries or its root pointer is present.
   The caller retains ownership of exif. */
bool photoc_jpeg_exif_has_gps(const photoc_jpeg_exif *exif);

/* Number of bytes added when attaching this EXIF to an encoded JPEG.
   Returns zero for NULL EXIF. The caller retains ownership of exif. */
photoc_jpeg_edit_result
photoc_jpeg_exif_output_overhead(const photoc_jpeg_exif *exif, uint64_t *bytes);

/* Remove all GPS IFD entries and the GPS pointer from this in-memory copy.
   The source JPEG and any other EXIF document remain unchanged. */
photoc_jpeg_edit_result photoc_jpeg_exif_remove_gps(photoc_jpeg_exif *exif);

/* Write a new JPEG to destination_path, replacing its EXIF APP1 segment (or
   inserting one after SOI). All other JPEG bytes, including compressed scan
   data, are copied verbatim. The source is never modified. A temporary file
   in the destination directory is fully written and verified before an atomic
   no-overwrite rename. An existing destination is never replaced. On an I/O
   failure, errno describes the failed operation. The caller retains exif. */
photoc_jpeg_edit_result
photoc_jpeg_write_with_exif(const char *source_path,
                            const char *destination_path,
                            const photoc_jpeg_exif *exif);

/* Save encoded JPEG bytes as a new file, optionally adding an EXIF copy.
   The caller retains the buffer and EXIF object. A temporary file beside the
   destination is synced and validated first; the destination is never
   overwritten. Without EXIF, the temporary file becomes the output. */
photoc_jpeg_edit_result
photoc_jpeg_write_encoded(const char *destination_path,
                          const photoc_jpeg_buffer *encoded,
                          const photoc_jpeg_exif *exif);

/* Replace source_path with an edited JPEG only after writing, syncing, and
   verifying a temporary JPEG beside it. Preserves POSIX permission bits and
   group ownership, failing before replacement if either cannot be set.
   Refuses symlinks, hard-linked files, files owned by another user, and a
   source whose identity or contents changed since exif was loaded. The
   original path remains untouched on any failure before atomic rename.
   Other metadata such as ACLs and extended attributes is not preserved. */
photoc_jpeg_edit_result
photoc_jpeg_replace_with_exif(const char *source_path,
                              const photoc_jpeg_exif *exif);

void photoc_jpeg_exif_free(photoc_jpeg_exif *exif);

/* Static diagnostic text; never free it. */
const char *photoc_jpeg_edit_result_message(photoc_jpeg_edit_result result);

#endif
