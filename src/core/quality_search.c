#include "photoc/quality_search.h"

#include <errno.h>
#include <stddef.h>

int photoc_quality_search(uint64_t target, int minimum_quality,
                          photoc_quality_probe_fn probe, void *user_data,
                          photoc_quality_choice *choice)
{
    if (target == 0 || minimum_quality < 1 || minimum_quality > 100 ||
        probe == NULL || choice == NULL) {
        errno = EINVAL;
        return -1;
    }

    uint64_t size = 0;
    if (probe(minimum_quality, &size, user_data) != 0) {
        return -1;
    }
    photoc_quality_choice best = {minimum_quality, size, size <= target};
    if (!best.target_met || minimum_quality == 100) {
        *choice = best;
        return 0;
    }

    if (probe(100, &size, user_data) != 0) {
        return -1;
    }
    if (size <= target) {
        *choice = (photoc_quality_choice){100, size, true};
        return 0;
    }

    int low = minimum_quality + 1;
    int high = 99;
    while (low <= high) {
        int middle = low + (high - low) / 2;
        if (probe(middle, &size, user_data) != 0) {
            return -1;
        }
        if (size <= target) {
            best = (photoc_quality_choice){middle, size, true};
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    *choice = best;
    return 0;
}
