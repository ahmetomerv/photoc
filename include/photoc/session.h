#ifndef PHOTOC_SESSION_H
#define PHOTOC_SESSION_H

#include "photoc/photo.h"

#include <stddef.h>
#include <stdint.h>

#define PHOTOC_SESSION_DEFAULT_GAP_MINUTES 60u
#define PHOTOC_SESSION_ID_MISSING 0u

typedef enum {
    PHOTOC_SESSION_OK = 0,
    PHOTOC_SESSION_INVALID_ARGUMENT,
    PHOTOC_SESSION_INVALID_TIMESTAMP,
    PHOTOC_SESSION_OUT_OF_ORDER
} photoc_session_result;

/* Groups photos in their input order using local EXIF capture timestamps.
   Valid timestamps must be nondecreasing, including across missing entries.
   A gap greater than max_gap_minutes starts the next session; an exact gap
   stays in the same session. A NULL timestamp gets ID 0 and breaks the chain,
   so the next dated photo starts a new session. Malformed non-NULL timestamps
   are errors. IDs for dated photos start at 1 and increase sequentially.
   No timezone conversion is performed.

   photos and session_ids are borrowed; session_ids must hold count elements.
   No memory is allocated. On error session_ids is left unchanged. For count 0,
   both pointers may be NULL. Pass PHOTOC_SESSION_DEFAULT_GAP_MINUTES for the
   default threshold. */
photoc_session_result photoc_session_group(const Photo *photos, size_t count,
                                           uint32_t max_gap_minutes,
                                           size_t *session_ids);

#endif
