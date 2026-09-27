#ifndef PHOTOC_TIMESTAMP_H
#define PHOTOC_TIMESTAMP_H

#include <stdbool.h>
#include <stdint.h>

/* Accepts a real local EXIF timestamp in YYYY:MM:DD HH:MM:SS form.
   No timezone is inferred. */
bool photoc_timestamp_is_valid(const char *text);

/* Converts a valid local EXIF timestamp to seconds since 0001-01-01
   00:00:00, without timezone adjustment. Returns false and leaves seconds
   unchanged for invalid input. */
bool photoc_timestamp_to_seconds(const char *text, uint64_t *seconds);

#endif
