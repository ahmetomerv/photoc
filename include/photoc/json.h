#ifndef PHOTOC_JSON_H
#define PHOTOC_JSON_H

#include <stdio.h>

/* Writes a JSON string to stream, or null when value is NULL. Valid UTF-8 is
   preserved; invalid byte sequences are replaced with U+FFFD. Returns 0 on
   success or -1 on a stream write error. No memory is allocated. */
int photoc_json_write_string(FILE *stream, const char *value);

#endif
