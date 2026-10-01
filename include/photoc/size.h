#ifndef PHOTOC_SIZE_H
#define PHOTOC_SIZE_H

#define PHOTOC_SIZE_TEXT_CAPACITY 32

/* Write a decimal, human-readable byte count to caller-owned storage.
   The input must be finite and nonnegative. The buffer must have at least
   PHOTOC_SIZE_TEXT_CAPACITY bytes. No memory is allocated. */
void photoc_size_format(double bytes,
                        char buffer[PHOTOC_SIZE_TEXT_CAPACITY]);

#endif
