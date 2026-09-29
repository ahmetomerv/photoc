#define _POSIX_C_SOURCE 200809L

#include "photoc/scan.h"

#include "photoc/fs.h"
#include "photoc/thread_pool.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum { SCAN_BATCH_SIZE = 32, SCAN_POOL_START_MIN = 16, SCAN_PARALLEL_MIN = 8 };

typedef struct {
    char *path; /* Owned; NULL represents a unsupported regular file. */
    Photo photo;
    photoc_metadata_result result;
    int system_errno;
} scan_item;

typedef struct {
    photoc_scan_photo_fn on_photo;
    photoc_scan_warning_fn on_warning;
    void *user_data;
    photoc_scan_stats *stats;
    scan_item items[SCAN_BATCH_SIZE];
    size_t count;
    size_t workers;
    unsigned int formats;
    photoc_thread_pool *pool;
    bool pool_attempted;
    int error;
} scan_context;

static void load_item(size_t index, void *user_data)
{
    scan_context *context = user_data;
    scan_item *item = &context->items[index];
    if (item->path == NULL)
        return;
    item->result = photo_load_metadata(item->path, &item->photo);
    item->system_errno = item->result == PHOTOC_METADATA_IO_ERROR ? errno : 0;
}

static void clear_batch(scan_context *context)
{
    for (size_t i = 0; i < context->count; ++i) {
        photo_cleanup(&context->items[i].photo);
        free(context->items[i].path);
        context->items[i] = (scan_item){0};
    }
    context->count = 0;
}

static int flush_batch(scan_context *context)
{
    size_t photo_count = 0;
    for (size_t i = 0; i < context->count; ++i) {
        if (context->items[i].path != NULL)
            ++photo_count;
    }
    if (photo_count >= SCAN_POOL_START_MIN && context->workers != 1 &&
        !context->pool_attempted) {
        context->pool_attempted = true;
        context->pool = photoc_thread_pool_create(context->workers);
        /* Resource-constrained hosts can still complete the scan serially. */
    }
    if (photo_count >= SCAN_PARALLEL_MIN && context->pool != NULL) {
        if (photoc_thread_pool_run(context->pool, context->count, load_item,
                                   context) != 0) {
            context->error = errno;
            clear_batch(context);
            return -1;
        }
    } else {
        for (size_t i = 0; i < context->count; ++i)
            load_item(i, context);
    }

    int result = 0;
    /* Workers never call user callbacks or update aggregate counters. Replay
       in traversal order, with partial counts stopping at the last callback. */
    for (size_t i = 0; i < context->count; ++i) {
        scan_item *item = &context->items[i];
        ++context->stats->files_visited;
        if (item->path == NULL) {
            ++context->stats->skipped_files;
            continue;
        }
        ++context->stats->metadata_files_found;
        if (photoc_format_from_path(item->path) == PHOTOC_FORMAT_JPEG)
            ++context->stats->jpeg_files_found;
        else
            ++context->stats->arw_files_found;
        if (item->result != PHOTOC_METADATA_OK) {
            ++context->stats->errors;
            ++context->stats->skipped_files;
            if (context->on_warning != NULL) {
                context->on_warning(item->path, item->result,
                                    item->system_errno, context->user_data);
            }
            if (item->result == PHOTOC_METADATA_NO_MEMORY) {
                context->error = ENOMEM;
                result = -1;
                break;
            }
        } else {
            ++context->stats->photos_parsed;
            if (!context->on_photo(&item->photo, context->user_data)) {
                result = 1;
                break;
            }
        }
    }
    clear_batch(context);
    return result;
}

static bool scan_entry(const char *path, photoc_fs_type type, void *user_data)
{
    scan_context *context = user_data;
    if (type != PHOTOC_FS_FILE) {
        /* Deliver earlier callbacks before a recursive descent can fail. */
        return type != PHOTOC_FS_DIRECTORY || flush_batch(context) == 0;
    }
    scan_item *item = &context->items[context->count];
    if (photoc_format_is_selected(path, context->formats)) {
        item->path = strdup(path);
        if (item->path == NULL) {
            context->error = ENOMEM;
            return false;
        }
    }
    ++context->count;
    return context->count < SCAN_BATCH_SIZE || flush_batch(context) == 0;
}

static int scan_directory(const char *directory, bool recursive, size_t workers,
                          unsigned int formats, photoc_scan_photo_fn on_photo,
                          photoc_scan_warning_fn on_warning, void *user_data,
                          photoc_scan_stats *stats)
{
    if (directory == NULL || directory[0] == '\0' || on_photo == NULL ||
        stats == NULL || workers > PHOTOC_MAX_WORKERS || formats == 0 ||
        (formats & ~PHOTOC_FORMATS_METADATA) != 0) {
        errno = EINVAL;
        return -1;
    }
    *stats = (photoc_scan_stats){0};
    scan_context context = {.on_photo = on_photo,
                            .on_warning = on_warning,
                            .user_data = user_data,
                            .stats = stats,
                            .workers = workers,
                            .formats = formats};
    int result = recursive
                     ? photoc_fs_walk_recursive(directory, scan_entry, &context)
                     : photoc_fs_walk(directory, scan_entry, &context);
    int saved_errno = errno;
    if (context.error == 0 && result != 1) {
        int batch_result = flush_batch(&context);
        if (batch_result != 0)
            result = batch_result;
    }
    clear_batch(&context);
    photoc_thread_pool_destroy(context.pool);
    if (context.error != 0) {
        errno = context.error;
        return -1;
    }
    if (result == -1)
        errno = saved_errno;
    return result;
}

int photoc_scan_directory(const char *directory, bool recursive,
                          photoc_scan_photo_fn on_photo,
                          photoc_scan_warning_fn on_warning, void *user_data,
                          photoc_scan_stats *stats)
{
    return photoc_scan_directory_with_workers(directory, recursive, 0, on_photo,
                                              on_warning, user_data, stats);
}

int photoc_scan_directory_with_workers(const char *directory, bool recursive,
                                       size_t workers,
                                       photoc_scan_photo_fn on_photo,
                                       photoc_scan_warning_fn on_warning,
                                       void *user_data,
                                       photoc_scan_stats *stats)
{
    return scan_directory(directory, recursive, workers,
                          PHOTOC_FORMATS_METADATA, on_photo, on_warning,
                          user_data, stats);
}

int photoc_scan_directory_filtered(const char *directory, bool recursive,
                                   unsigned int formats,
                                   photoc_scan_photo_fn on_photo,
                                   photoc_scan_warning_fn on_warning,
                                   void *user_data, photoc_scan_stats *stats)
{
    return scan_directory(directory, recursive, 0, formats, on_photo,
                          on_warning, user_data, stats);
}
