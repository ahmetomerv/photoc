#include "photoc/quality_search.h"

#include <errno.h>
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

typedef struct {
    int probes;
    int fail_at;
    uint64_t overhead;
} test_probe;

static int linear_size(int quality, uint64_t *size, void *user_data)
{
    test_probe *state = user_data;
    ++state->probes;
    if (quality == state->fail_at) {
        errno = EIO;
        return -1;
    }
    *size = (uint64_t)quality * 100 + state->overhead;
    return 0;
}

int main(void)
{
    photoc_quality_choice choice = {0};
    test_probe state = {.overhead = 42};

    CHECK(photoc_quality_search(5042, 20, linear_size, &state, &choice) == 0);
    CHECK(choice.quality == 50);
    CHECK(choice.size == 5042);
    CHECK(choice.target_met);
    CHECK(state.probes <= 9);

    state.probes = 0;
    CHECK(photoc_quality_search(2042, 20, linear_size, &state, &choice) == 0);
    CHECK(choice.quality == 20);
    CHECK(choice.size == 2042);
    CHECK(choice.target_met);
    CHECK(state.probes <= 9);

    state.probes = 0;
    CHECK(photoc_quality_search(5099, 20, linear_size, &state, &choice) == 0);
    CHECK(choice.quality == 50);
    CHECK(choice.size == 5042);
    CHECK(choice.target_met);
    CHECK(state.probes <= 9);

    state.probes = 0;
    CHECK(photoc_quality_search(2041, 20, linear_size, &state, &choice) == 0);
    CHECK(choice.quality == 20);
    CHECK(choice.size == 2042);
    CHECK(!choice.target_met);
    CHECK(state.probes == 1);

    state.probes = 0;
    CHECK(photoc_quality_search(10042, 20, linear_size, &state, &choice) == 0);
    CHECK(choice.quality == 100);
    CHECK(choice.size == 10042);
    CHECK(choice.target_met);
    CHECK(state.probes == 2);

    state.probes = 0;
    CHECK(photoc_quality_search(10042, 100, linear_size, &state, &choice) == 0);
    CHECK(choice.quality == 100);
    CHECK(state.probes == 1);

    state.fail_at = 100;
    choice.quality = 7;
    errno = 0;
    CHECK(photoc_quality_search(5042, 20, linear_size, &state, &choice) == -1);
    CHECK(errno == EIO);
    CHECK(choice.quality == 7);

    CHECK(photoc_quality_search(0, 20, linear_size, &state, &choice) == -1);
    CHECK(photoc_quality_search(1000, 0, linear_size, &state, &choice) == -1);
    CHECK(photoc_quality_search(1000, 101, linear_size, &state, &choice) == -1);
    CHECK(photoc_quality_search(1000, 20, NULL, &state, &choice) == -1);
    CHECK(photoc_quality_search(1000, 20, linear_size, &state, NULL) == -1);

    return failures == 0 ? 0 : 1;
}
