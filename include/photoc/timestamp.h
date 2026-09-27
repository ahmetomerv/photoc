#ifndef PHOTOC_TIMESTAMP_H
#define PHOTOC_TIMESTAMP_H

#include <stdbool.h>

/* Accepts a real local EXIF timestamp in YYYY:MM:DD HH:MM:SS form.
   No timezone is inferred. */
bool photoc_timestamp_is_valid(const char *text);

#endif
