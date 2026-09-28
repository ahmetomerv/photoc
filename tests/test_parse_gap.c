#include "photoc/parse.h"

#include <stdint.h>
#include <stdio.h>

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++failures;                                                        \
        }                                                                      \
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

static void valid_size(const char *text, uint64_t expected)
{
    uint64_t bytes = 17;
    CHECK(photoc_parse_size_bytes(text, &bytes));
    CHECK(bytes == expected);
}

static void invalid_size(const char *text)
{
    uint64_t bytes = 17;
    CHECK(!photoc_parse_size_bytes(text, &bytes));
    CHECK(bytes == 17);
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

    valid_size("1", 1);
    valid_size("2MB", 2000000);
    valid_size("2KB", 2000);
    valid_size("3KiB", 3072);
    valid_size("4MiB", UINT64_C(4194304));
    valid_size("5GB", UINT64_C(5000000000));
    valid_size("6GiB", UINT64_C(6442450944));
    valid_size("18446744073709551615B", UINT64_MAX);
    invalid_size(NULL);
    invalid_size("");
    invalid_size("0MB");
    invalid_size("-1MB");
    invalid_size("1.5MB");
    invalid_size("2mb");
    invalid_size("2TB");
    invalid_size("18446744073709551616B");
    invalid_size("18446744073709551615KB");
    CHECK(!photoc_parse_size_bytes("2MB", NULL));

    if (failures != 0) {
        fprintf(stderr, "%d gap parser test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
