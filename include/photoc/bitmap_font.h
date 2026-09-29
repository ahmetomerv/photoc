#ifndef PHOTOC_BITMAP_FONT_H
#define PHOTOC_BITMAP_FONT_H

#include "photoc/image.h"

#include <stdint.h>

/* Original 5x7 ASCII bitmap glyphs, covered by photoc's MIT license. Draws
   black text into a caller-owned RGB image. Unsupported UTF-8 code points
   become '?'; long lines end in '~'. No allocation or platform font API. */
void photoc_bitmap_text(photoc_image *image, uint32_t x, uint32_t y,
                        const char *text, uint32_t max_width,
                        uint32_t scale);

#endif
