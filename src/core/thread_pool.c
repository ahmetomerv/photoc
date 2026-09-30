#define _POSIX_C_SOURCE 200809L

#include "photoc/thread_pool.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

struct photoc_thread_pool {
    pthread_mutex_t mutex;
    pthread_cond_t work;
    pthread_cond_t done;
    pthread_t threads[PHOTOC_MAX_WORKERS];
    size_t thread_count;
    size_t next;
    size_t count;
    size_t remaining;
    photoc_thread_task_fn task;
    void *user_data;
    bool active;
    bool stopping;
};

static void *worker_main(void *user_data)
{
    photoc_thread_pool *pool = user_data;
    pthread_mutex_lock(&pool->mutex);
    for (;;) {
        while (!pool->stopping &&
               (!pool->active || pool->next == pool->count)) {
            pthread_cond_wait(&pool->work, &pool->mutex);
        }
        if (pool->stopping) {
            break;
        }
        size_t index = pool->next++;
        photoc_thread_task_fn task = pool->task;
        void *data = pool->user_data;
        pthread_mutex_unlock(&pool->mutex);
        task(index, data);
        pthread_mutex_lock(&pool->mutex);
        if (--pool->remaining == 0) {
            pool->active = false;
            pthread_cond_broadcast(&pool->done);
        }
    }
    pthread_mutex_unlock(&pool->mutex);
    return NULL;
}

photoc_thread_pool *photoc_thread_pool_create(size_t workers)
{
    if (workers == 0)
        workers = PHOTOC_DEFAULT_WORKERS;
    if (workers > PHOTOC_MAX_WORKERS) {
        errno = EINVAL;
        return NULL;
    }
    photoc_thread_pool *pool = calloc(1, sizeof(*pool));
    if (pool == NULL)
        return NULL;
    int error = pthread_mutex_init(&pool->mutex, NULL);
    if (error != 0)
        goto fail_mutex;
    error = pthread_cond_init(&pool->work, NULL);
    if (error != 0)
        goto fail_work;
    error = pthread_cond_init(&pool->done, NULL);
    if (error != 0)
        goto fail_done;
    if (workers > 1) {
        for (size_t i = 0; i < workers; ++i) {
            error = pthread_create(&pool->threads[i], NULL, worker_main, pool);
            if (error != 0) {
                photoc_thread_pool_destroy(pool);
                errno = error;
                return NULL;
            }
            ++pool->thread_count;
        }
    }
    return pool;

fail_done:
    pthread_cond_destroy(&pool->work);
fail_work:
    pthread_mutex_destroy(&pool->mutex);
fail_mutex:
    free(pool);
    errno = error;
    return NULL;
}

int photoc_thread_pool_run(photoc_thread_pool *pool, size_t count,
                           photoc_thread_task_fn task, void *user_data)
{
    return photoc_thread_pool_run_poll(pool, count, task, user_data, NULL,
                                       NULL);
}

int photoc_thread_pool_run_poll(photoc_thread_pool *pool, size_t count,
                                photoc_thread_task_fn task, void *user_data,
                                void (*poll)(void *), void *poll_data)
{
    if (pool == NULL || task == NULL) {
        errno = EINVAL;
        return -1;
    }
    pthread_mutex_lock(&pool->mutex);
    if (pool->active || pool->stopping) {
        pthread_mutex_unlock(&pool->mutex);
        errno = EBUSY;
        return -1;
    }
    if (count == 0) {
        pthread_mutex_unlock(&pool->mutex);
        return 0;
    }
    pool->active = true;
    if (pool->thread_count == 0) {
        pthread_mutex_unlock(&pool->mutex);
        for (size_t i = 0; i < count; ++i) {
            task(i, user_data);
            if (poll != NULL)
                poll(poll_data);
        }
        pthread_mutex_lock(&pool->mutex);
        pool->active = false;
        pthread_cond_broadcast(&pool->done);
    } else {
        pool->next = 0;
        pool->count = count;
        pool->remaining = count;
        pool->task = task;
        pool->user_data = user_data;
        pthread_cond_broadcast(&pool->work);
        while (pool->active) {
            if (poll == NULL) {
                pthread_cond_wait(&pool->done, &pool->mutex);
            } else {
                struct timespec deadline;
                if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
                    pthread_cond_wait(&pool->done, &pool->mutex);
                } else {
                    deadline.tv_nsec += 90000000L;
                    if (deadline.tv_nsec >= 1000000000L) {
                        ++deadline.tv_sec;
                        deadline.tv_nsec -= 1000000000L;
                    }
                    pthread_cond_timedwait(&pool->done, &pool->mutex,
                                           &deadline);
                }
                pthread_mutex_unlock(&pool->mutex);
                poll(poll_data);
                pthread_mutex_lock(&pool->mutex);
            }
        }
        pool->task = NULL;
        pool->user_data = NULL;
    }
    pthread_mutex_unlock(&pool->mutex);
    return 0;
}

void photoc_thread_pool_destroy(photoc_thread_pool *pool)
{
    if (pool == NULL)
        return;
    pthread_mutex_lock(&pool->mutex);
    while (pool->active)
        pthread_cond_wait(&pool->done, &pool->mutex);
    pool->stopping = true;
    pthread_cond_broadcast(&pool->work);
    pthread_mutex_unlock(&pool->mutex);
    for (size_t i = 0; i < pool->thread_count; ++i) {
        pthread_join(pool->threads[i], NULL);
    }
    pthread_cond_destroy(&pool->done);
    pthread_cond_destroy(&pool->work);
    pthread_mutex_destroy(&pool->mutex);
    free(pool);
}
