#include "photoc/parse.h"

#include <stdint.h>
#include <stdio.h>

static int failures;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

static void valid(const char *text, uint32_t expected)
{
    uint32_t minutes = 17;
    CHECK(photoc_parse_gap_minutes(text, &minutes));
    CHECK(minutes == expected);
}

static void invalid(const char *text)
{
    uint32_t minutes = 17;
    CHECK(!photoc_parse_gap_minutes(text, &minutes));
    CHECK(minutes == 17);
}

int main(void)
{
    valid("30m", 30);
    valid("2h", 120);
    valid("0m", 0);
    valid("001h", 60);
    valid("4294967295m", UINT32_MAX);
    valid("71582788h", 4294967280u);

    invalid(NULL);
    invalid("");
    invalid("30");
    invalid("m");
    invalid("30M");
    invalid("1.5h");
    invalid("-30m");
    invalid("30ms");
    invalid(" 30m");
    invalid("4294967296m");
    invalid("71582789h");
    CHECK(!photoc_parse_gap_minutes("30m", NULL));

    if (failures != 0) {
        fprintf(stderr, "%d gap parser test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
