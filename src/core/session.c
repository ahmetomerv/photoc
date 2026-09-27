#include "photoc/session.h"

#include "photoc/timestamp.h"

#include <stdbool.h>
#include <stdint.h>

photoc_session_result photoc_session_group(const Photo *photos, size_t count,
                                           uint32_t max_gap_minutes,
                                           size_t *session_ids)
{
    if (count == 0) {
        return PHOTOC_SESSION_OK;
    }
    if (photos == NULL || session_ids == NULL) {
        return PHOTOC_SESSION_INVALID_ARGUMENT;
    }

    /* Validate the whole input before writing any result. Missing entries do
       not hide a reversal in the dated photos on either side of them. */
    uint64_t previous = 0;
    bool have_previous = false;
    for (size_t i = 0; i < count; ++i) {
        if (photos[i].capture_timestamp == NULL) {
            continue;
        }
        uint64_t current;
        if (!photoc_timestamp_to_seconds(photos[i].capture_timestamp,
                                         &current)) {
            return PHOTOC_SESSION_INVALID_TIMESTAMP;
        }
        if (have_previous && current < previous) {
            return PHOTOC_SESSION_OUT_OF_ORDER;
        }
        previous = current;
        have_previous = true;
    }

    const uint64_t maximum_gap_seconds = (uint64_t)max_gap_minutes * 60u;
    size_t current_id = PHOTOC_SESSION_ID_MISSING;
    have_previous = false;
    for (size_t i = 0; i < count; ++i) {
        if (photos[i].capture_timestamp == NULL) {
            session_ids[i] = PHOTOC_SESSION_ID_MISSING;
            have_previous = false;
            continue;
        }
        uint64_t current;
        (void)photoc_timestamp_to_seconds(photos[i].capture_timestamp,
                                          &current);
        if (!have_previous || current - previous > maximum_gap_seconds) {
            ++current_id;
        }
        session_ids[i] = current_id;
        previous = current;
        have_previous = true;
    }
    return PHOTOC_SESSION_OK;
}
