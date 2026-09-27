#include "photoc/session.h"

#include "photoc/timestamp.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

static char *copy_text(const char *text)
{
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, text, length + 1);
    }
    return copy;
}

static void check_group(const char *const *timestamps, size_t count,
                        uint32_t gap_minutes, photoc_session_result expected,
                        const size_t *expected_ids)
{
    Photo photos[8] = {0};
    size_t ids[8];
    CHECK(count <= 8);
    if (count > 8) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        ids[i] = 999;
        if (timestamps[i] != NULL) {
            photos[i].capture_timestamp = copy_text(timestamps[i]);
            if (photos[i].capture_timestamp == NULL) {
                CHECK(false);
                for (size_t j = 0; j < i; ++j) {
                    photo_cleanup(&photos[j]);
                }
                return;
            }
        }
    }
    CHECK(photoc_session_group(photos, count, gap_minutes, ids) == expected);
    for (size_t i = 0; i < count; ++i) {
        CHECK(ids[i] == (expected == PHOTOC_SESSION_OK ?
                         expected_ids[i] : 999));
        photo_cleanup(&photos[i]);
    }
}

static void test_same_and_multiple_sessions(void)
{
    const char *same[] = {
        "2026:09:27 10:00:00", "2026:09:27 10:50:00",
        "2026:09:27 11:40:00", "2026:09:27 11:40:00"
    };
    const size_t same_ids[] = {1, 1, 1, 1};
    check_group(same, 4, PHOTOC_SESSION_DEFAULT_GAP_MINUTES,
                PHOTOC_SESSION_OK, same_ids);

    const char *multiple[] = {
        "2026:09:27 10:00:00", "2026:09:27 10:30:00",
        "2026:09:27 12:00:00", "2026:09:28 12:00:00"
    };
    const size_t multiple_ids[] = {1, 1, 2, 3};
    check_group(multiple, 4, PHOTOC_SESSION_DEFAULT_GAP_MINUTES,
                PHOTOC_SESSION_OK, multiple_ids);
}

static void test_boundaries_and_rollover(void)
{
    const char *boundary[] = {
        "2026:09:27 10:00:00", "2026:09:27 11:00:00",
        "2026:09:27 12:00:01"
    };
    const size_t boundary_ids[] = {1, 1, 2};
    check_group(boundary, 3, 60, PHOTOC_SESSION_OK, boundary_ids);

    const char *custom[] = {
        "2026:09:27 10:00:00", "2026:09:27 10:15:00",
        "2026:09:27 10:30:01"
    };
    const size_t custom_ids[] = {1, 1, 2};
    check_group(custom, 3, 15, PHOTOC_SESSION_OK, custom_ids);

    const char *zero_gap[] = {
        "2026:09:27 10:00:00", "2026:09:27 10:00:00",
        "2026:09:27 10:00:01"
    };
    const size_t zero_gap_ids[] = {1, 1, 2};
    check_group(zero_gap, 3, 0, PHOTOC_SESSION_OK, zero_gap_ids);

    const char *leap_day[] = {
        "2024:02:29 23:30:00", "2024:03:01 00:30:00",
        "2024:03:01 01:30:01"
    };
    const size_t leap_ids[] = {1, 1, 2};
    check_group(leap_day, 3, 60, PHOTOC_SESSION_OK, leap_ids);
}

static void test_missing_timestamps(void)
{
    const char *missing[] = {
        NULL, "2026:09:27 10:00:00", "2026:09:27 10:30:00",
        NULL, "2026:09:27 10:40:00", NULL
    };
    const size_t ids[] = {0, 1, 1, 0, 2, 0};
    check_group(missing, 6, 60, PHOTOC_SESSION_OK, ids);

    const char *all_missing[] = {NULL, NULL};
    const size_t missing_ids[] = {0, 0};
    check_group(all_missing, 2, 60, PHOTOC_SESSION_OK, missing_ids);
}

static void test_invalid_input(void)
{
    const char *out_of_order[] = {
        "2026:09:27 10:00:00", "2026:09:27 09:59:59"
    };
    check_group(out_of_order, 2, 60, PHOTOC_SESSION_OUT_OF_ORDER, NULL);

    const char *reversal_around_missing[] = {
        "2026:09:27 10:00:00", NULL, "2026:09:27 09:00:00"
    };
    check_group(reversal_around_missing, 3, 60,
                PHOTOC_SESSION_OUT_OF_ORDER, NULL);

    const char *malformed[] = {
        "2026:09:27 10:00:00", "2026:02:30 10:30:00"
    };
    check_group(malformed, 2, 60, PHOTOC_SESSION_INVALID_TIMESTAMP, NULL);

    Photo one = {0};
    size_t id = 77;
    CHECK(photoc_session_group(NULL, 0, 60, NULL) == PHOTOC_SESSION_OK);
    CHECK(photoc_session_group(NULL, 1, 60, &id) ==
          PHOTOC_SESSION_INVALID_ARGUMENT && id == 77);
    CHECK(photoc_session_group(&one, 1, 60, NULL) ==
          PHOTOC_SESSION_INVALID_ARGUMENT);
    CHECK(photoc_session_group(&one, 1, 60, &id) == PHOTOC_SESSION_OK &&
          id == PHOTOC_SESSION_ID_MISSING);
}

static void test_timestamp_conversion(void)
{
    uint64_t first = 0;
    uint64_t second = 0;
    CHECK(photoc_timestamp_to_seconds("2024:02:29 23:59:59", &first));
    CHECK(photoc_timestamp_to_seconds("2024:03:01 00:00:00", &second));
    CHECK(second == first + 1);
    CHECK(!photoc_timestamp_to_seconds("2023:02:29 00:00:00", &second));
    CHECK(second == first + 1);
    CHECK(!photoc_timestamp_to_seconds("2024:03:01 00:00:00", NULL));
}

int main(void)
{
    test_same_and_multiple_sessions();
    test_boundaries_and_rollover();
    test_missing_timestamps();
    test_invalid_input();
    test_timestamp_conversion();
    if (failures != 0) {
        fprintf(stderr, "%d session test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
