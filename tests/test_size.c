#include "photoc/size.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(double bytes, const char *expected)
{
    char actual[PHOTOC_SIZE_TEXT_CAPACITY];
    photoc_size_format(bytes, actual);
    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "%.17g bytes: expected '%s', got '%s'\n", bytes,
                expected, actual);
        ++failures;
    }
}

int main(void)
{
    check(0, "0 B");
    check(999, "999 B");
    check(1000, "1.0 KB");
    check(1534, "1.5 KB");
    check(999949, "999.9 KB");
    check(999950, "1.0 MB");
    check(2300000, "2.3 MB");
    check(1000000000, "1.0 GB");
    check(1000000000000.0, "1.0 TB");
    check(1000000000000000.0, "1.0 PB");
    check(1000000000000000000.0, "1.0 EB");
    check((double)UINT64_MAX, "18.4 EB");
    check(531.5, "532 B");
    check(1525.0, "1.5 KB");
    return failures != 0;
}
