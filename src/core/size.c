#include "photoc/size.h"

#include <stdio.h>

void photoc_size_format(double bytes, char buffer[PHOTOC_SIZE_TEXT_CAPACITY])
{
    static const char *const units[] = {"B",  "KB", "MB", "GB",
                                        "TB", "PB", "EB"};
    size_t unit = 0;
    while (bytes >= 1000.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        bytes /= 1000.0;
        ++unit;
    }
    /* Promote values that round to 1000.0 in the current unit. */
    if (unit != 0 && bytes >= 999.95 &&
        unit + 1 < sizeof(units) / sizeof(units[0])) {
        bytes /= 1000.0;
        ++unit;
    }
    if (unit == 0)
        snprintf(buffer, PHOTOC_SIZE_TEXT_CAPACITY, "%.0f %s", bytes,
                 units[unit]);
    else
        snprintf(buffer, PHOTOC_SIZE_TEXT_CAPACITY, "%.1f %s", bytes,
                 units[unit]);
}
