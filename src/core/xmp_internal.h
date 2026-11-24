#ifndef PHOTOC_XMP_INTERNAL_H
#define PHOTOC_XMP_INTERNAL_H

#include "photoc/jpeg_write.h"

#include <stdbool.h>
#include <stddef.h>

/* Parse a borrowed XMP document and remove recognized standard location,
   creator/owner, serial-number, and document-ID properties, matching privacy
   scrubbing. Malformed XML or a DTD returns UNSUPPORTED_XMP. When *changed is
   true, *out owns the reserialized UTF-8 document (free with free()); when
   nothing matched, *out is NULL and the original stays valid as is. */
photoc_jpeg_edit_result
photoc_xmp_remove_private(const char *xml, size_t length, unsigned char **out,
                          size_t *out_length, bool *changed);

#endif
