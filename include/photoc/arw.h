#ifndef PHOTOC_ARW_H
#define PHOTOC_ARW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "photoc/image.h"
#include "photoc/jpeg_write.h"

/* Embedded previews larger than this are refused. Camera previews are a few
   megabytes at most; the cap bounds memory for untrusted files. */
#define PHOTOC_ARW_PREVIEW_MAX_BYTES (UINT64_C(64) * 1024 * 1024)

/* Read the largest embedded JPEG (JPEGInterchangeFormat/Length) from a Sony
   ARW using the bounded classic-TIFF reader. CFA/linear RAW directories are
   never treated as previews. The preview must lie within the file, be
   nonempty, start with a JPEG SOI marker, and be at most max_bytes (0 selects
   PHOTOC_ARW_PREVIEW_MAX_BYTES). Symlinks and non-regular files are refused.
   The ARW is never modified and no RAW sensor data is decoded.

   On success *jpeg owns malloc'd bytes the caller frees with free(), and
   *length is their count. On failure *jpeg is NULL: INVALID_ARW for a
   malformed container or a claimed preview that is unusable (missing half of
   the JPEGInterchange pair, offset 0, zero length, out of range, or not JPEG
   SOI), NO_PREVIEW when no preview tags are present, TOO_LARGE over the cap,
   or IO_ERROR with errno preserved. */
photoc_image_result photoc_arw_read_preview(const char *path,
                                            uint64_t max_bytes,
                                            unsigned char **jpeg,
                                            size_t *length);

/* Write a scrubbed copy of a Sony ARW to destination_path; the source is never
   modified and an existing destination is never replaced (IO_ERROR with
   errno EEXIST). Without privacy, only the EXIF GPS directory is removed: its
   pointer entry is dropped and the directory and its values are zeroed, so no
   coordinates remain. With privacy, standard Artist, CameraOwnerName,
   BodySerialNumber, LensSerialNumber, and ImageUniqueID entries are also
   removed from every directory, and recognized private standard XMP
   properties are removed with the packet padded back to its original size.

   Every edit is size-preserving: no image data, MakerNote, or other offset
   moves. Invalid strip/tile arrays or edits that overlap kept data fail with
   UNSAFE_ARW_LAYOUT; an XMP packet that cannot shrink back fails with
   UNSUPPORTED_XMP. The copy
   is written beside the destination, verified byte for byte and re-parsed,
   checked against source changes (UNSAFE_SOURCE), then published atomically.
   When nothing needs removing, report->changed is false and no file is
   written. report receives category flags only; maker_note marks MakerNote or
   Sony private data that may retain identifiers, and unhandled_metadata marks
   metadata inside embedded preview JPEGs, which is not edited. */
photoc_jpeg_edit_result photoc_arw_scrub_copy(const char *source_path,
                                              const char *destination_path,
                                              bool privacy,
                                              photoc_scrub_report *report);

#endif
