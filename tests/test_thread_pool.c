#define _POSIX_C_SOURCE 200809L
#include "photoc/thread_pool.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

static int failures;
#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

enum { TASK_COUNT = 128 };
typedef struct {
    unsigned int hits[TASK_COUNT];
    atomic_uint active;
    atomic_uint peak;
} task_results;

static void record_task(size_t index, void *user_data)
{
    task_results *results = user_data;
    unsigned int active = atomic_fetch_add(&results->active, 1) + 1;
    unsigned int peak = atomic_load(&results->peak);
    while (peak < active &&
           !atomic_compare_exchange_weak(&results->peak, &peak, active)) {
    }
    ++results->hits[index];
    struct timespec delay = {.tv_nsec = 200000};
    nanosleep(&delay, NULL);
    atomic_fetch_sub(&results->active, 1);
}

static void test_workers(size_t workers)
{
    photoc_thread_pool *pool = photoc_thread_pool_create(workers);
    CHECK(pool != NULL);
    if (pool == NULL)
        return;
    task_results results = {0};
    atomic_init(&results.active, 0);
    atomic_init(&results.peak, 0);
    CHECK(photoc_thread_pool_run(pool, 0, record_task, &results) == 0);
    for (unsigned int batch = 1; batch <= 4; ++batch) {
        CHECK(photoc_thread_pool_run(pool, TASK_COUNT, record_task, &results) ==
              0);
        CHECK(atomic_load(&results.active) == 0);
        CHECK(atomic_load(&results.peak) <=
              (workers == 0 ? PHOTOC_DEFAULT_WORKERS : workers));
        for (size_t i = 0; i < TASK_COUNT; ++i)
            CHECK(results.hits[i] == batch);
    }
    errno = 0;
    CHECK(photoc_thread_pool_run(pool, 1, NULL, NULL) == -1 && errno == EINVAL);
    /* An invalid run does not poison a reusable pool. */
    CHECK(photoc_thread_pool_run(pool, 1, record_task, &results) == 0);
    CHECK(results.hits[0] == 5);
    photoc_thread_pool_destroy(pool);
}

int main(void)
{
    errno = 0;
    CHECK(photoc_thread_pool_create(PHOTOC_MAX_WORKERS + 1) == NULL &&
          errno == EINVAL);
    CHECK(photoc_thread_pool_run(NULL, 1, record_task, NULL) == -1 &&
          errno == EINVAL);
    photoc_thread_pool_destroy(NULL);
    test_workers(1);
    test_workers(0);
    test_workers(2);
    test_workers(PHOTOC_MAX_WORKERS);
    for (size_t i = 0; i < 20; ++i) {
        photoc_thread_pool *pool = photoc_thread_pool_create(2);
        CHECK(pool != NULL);
        photoc_thread_pool_destroy(pool);
    }
    return failures == 0 ? 0 : 1;
}
