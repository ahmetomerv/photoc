#include "photoc/timestamp.h"

#include <stddef.h>
#include <string.h>

static unsigned int two_digits(const char *text)
{
    return (unsigned int)(text[0] - '0') * 10u + (unsigned int)(text[1] - '0');
}

bool photoc_timestamp_is_valid(const char *text)
{
    if (text == NULL || strlen(text) != 19 || text[4] != ':' ||
        text[7] != ':' || text[10] != ' ' || text[13] != ':' ||
        text[16] != ':') {
        return false;
    }
    for (size_t i = 0; i < 19; ++i) {
        if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) {
            continue;
        }
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
    }

    unsigned int year = (unsigned int)(text[0] - '0') * 1000u +
                        (unsigned int)(text[1] - '0') * 100u +
                        two_digits(text + 2);
    unsigned int month = two_digits(text + 5);
    unsigned int day = two_digits(text + 8);
    if (year == 0 || month == 0 || month > 12 || day == 0 ||
        two_digits(text + 11) > 23 || two_digits(text + 14) > 59 ||
        two_digits(text + 17) > 59) {
        return false;
    }
    static const unsigned int days_in_month[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    unsigned int maximum = days_in_month[month - 1];
    if (month == 2 && (year % 400 == 0 ||
        (year % 4 == 0 && year % 100 != 0))) {
        maximum = 29;
    }
    return day <= maximum;
}
