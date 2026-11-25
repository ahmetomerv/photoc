#ifndef PHOTOC_CHECK_INTERNAL_H
#define PHOTOC_CHECK_INTERNAL_H

#include "photoc/jpeg_check.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

/* The JPEG audit of photoc_jpeg_check_file on a borrowed stream positioned at
   the JPEG's first byte, reading at most length bytes. The caller owns file
   and checks identity/closing; any seekable stream, including fmemopen, works. */
photoc_check_result photoc_jpeg_check_stream(FILE *file, uint64_t length);

/* True when size and modification/change times are unchanged. */
bool photoc_check_same_file(const struct stat *before,
                            const struct stat *after);

#endif
