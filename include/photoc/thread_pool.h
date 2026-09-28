#ifndef PHOTOC_THREAD_POOL_H
#define PHOTOC_THREAD_POOL_H

#include <stddef.h>

#define PHOTOC_DEFAULT_WORKERS 2u
#define PHOTOC_MAX_WORKERS 8u

typedef struct photoc_thread_pool photoc_thread_pool;
typedef void (*photoc_thread_task_fn)(size_t index, void *user_data);

/* Creates an owned pool. Zero selects the conservative default of two workers;
   one runs tasks on the calling thread. Values above MAX_WORKERS are rejected.
   Returns NULL with errno set on invalid input, allocation, or thread failure.
   Release with destroy. No global pool or background work is retained. */
photoc_thread_pool *photoc_thread_pool_create(size_t workers);

/* Synchronously calls task once for each index in [0, count), in unspecified
   execution order. The pool stores no task queue: task and user_data are
   borrowed until all callbacks complete and this function returns. Each task
   must use separate result storage or synchronize shared writes. Present
   results afterward in index order for deterministic output.

   Returns 0 on completion, -1 with errno on invalid arguments or an already
   active pool. Tasks must not run/destroy their own pool. The owner must
   serialize run/destroy calls and keep the pool alive throughout each call. */
int photoc_thread_pool_run(photoc_thread_pool *pool, size_t count,
                           photoc_thread_task_fn task, void *user_data);

/* Waits for work to finish, joins every worker, and frees the pool. NULL is
   accepted. Call only after external callers have stopped using the pool. */
void photoc_thread_pool_destroy(photoc_thread_pool *pool);

#endif
