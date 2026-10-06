#ifndef PHOTOC_JSON_H
#define PHOTOC_JSON_H

#include <stdio.h>
#include <stdbool.h>

/* True if a NUL-terminated byte string is valid UTF-8. */
bool photoc_json_is_valid_utf8(const char *value);

/* Writes a JSON string to stream, or null when value is NULL. Valid UTF-8 is
   preserved; invalid byte sequences are replaced with U+FFFD. Returns 0 on
   success or -1 on a stream write error. No memory is allocated. */
int photoc_json_write_string(FILE *stream, const char *value);

#endif
