#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/duplicates.h"

#include "photoc/fs.h"
#include "photoc/hash.h"
#include "photoc/thread_pool.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Leading-byte sample used to skip full SHA-256 when a size group contains
   unique prefixes. Exact duplicates always share a prefix, so groups are
   unchanged; only files_hashed may drop for unique files. Keep this small so
   a large same-size cohort does not allocate megabytes of prefix storage. */
enum { DUPLICATE_PREFIX_BYTES = 64 };
enum { PATH_ARENA_CHUNK = 65536 };

/* SHA-256 of an empty message (FIPS 180-4). */
static const unsigned char empty_sha256[PHOTOC_SHA256_DIGEST_SIZE] = {
    0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4,
    0xc8, 0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b,
    0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55};

typedef struct {
    char *path;
    uint64_t size;
    unsigned char digest[PHOTOC_SHA256_DIGEST_SIZE];
    bool hashed;
    bool needs_hash;
    int hash_error;
} file_record;

typedef struct {
    char **chunks;
    size_t chunk_count;
    size_t chunk_capacity;
    char *current;
    size_t current_used;
    size_t current_capacity;
} path_arena;

typedef struct {
    file_record *files;
    size_t count;
    size_t capacity;
    size_t group_capacity;
    int error;
    path_arena paths;
    photoc_duplicates_result *result;
    photoc_duplicates_warning_fn on_warning;
    void *user_data;
    photoc_progress *progress;
} find_context;

typedef struct {
    size_t index;
    unsigned char *prefix;
    size_t length;
    const char *path;
} prefix_entry;

void photoc_duplicates_cleanup(photoc_duplicates_result *result)
{
    if (result == NULL) {
        return;
    }
    for (size_t i = 0; i < result->group_count; ++i) {
        for (size_t j = 0; j < result->groups[i].count; ++j) {
            free(result->groups[i].paths[j]);
        }
        free(result->groups[i].paths);
    }
    free(result->groups);
    *result = (photoc_duplicates_result){0};
}

static void path_arena_cleanup(path_arena *arena)
{
    if (arena == NULL) {
        return;
    }
    for (size_t i = 0; i < arena->chunk_count; ++i) {
        free(arena->chunks[i]);
    }
    free(arena->chunks);
    *arena = (path_arena){0};
}

static char *path_arena_copy(path_arena *arena, const char *path)
{
    size_t length = strlen(path);
    if (length == SIZE_MAX) {
        errno = EOVERFLOW;
        return NULL;
    }
    size_t need = length + 1;
    if (arena->current == NULL ||
        arena->current_used > arena->current_capacity ||
        need > arena->current_capacity - arena->current_used) {
        size_t capacity = PATH_ARENA_CHUNK;
        while (capacity < need) {
            if (capacity > SIZE_MAX / 2) {
                errno = EOVERFLOW;
                return NULL;
            }
            capacity *= 2;
        }
        if (arena->chunk_count == arena->chunk_capacity) {
            size_t next =
                arena->chunk_capacity == 0 ? 4 : arena->chunk_capacity * 2;
            if (next < arena->chunk_capacity ||
                next > SIZE_MAX / sizeof(*arena->chunks)) {
                errno = EOVERFLOW;
                return NULL;
            }
            char **chunks = realloc(arena->chunks, next * sizeof(*chunks));
            if (chunks == NULL) {
                return NULL;
            }
            arena->chunks = chunks;
            arena->chunk_capacity = next;
        }
        char *chunk = malloc(capacity);
        if (chunk == NULL) {
            return NULL;
        }
        arena->chunks[arena->chunk_count++] = chunk;
        arena->current = chunk;
        arena->current_used = 0;
        arena->current_capacity = capacity;
    }
    char *copy = arena->current + arena->current_used;
    memcpy(copy, path, need);
    arena->current_used += need;
    return copy;
}

static void warn_file(find_context *context, const char *path, int error)
{
    ++context->result->files_skipped;
    ++context->result->errors;
    if (context->on_warning != NULL) {
        photoc_progress_before_diagnostic(context->progress);
        context->on_warning(path, error, context->user_data);
    }
}

static bool collect_file(const char *path, photoc_fs_type type, void *user_data)
{
    find_context *context = user_data;
    if (photoc_progress_interrupted()) {
        context->error = EINTR;
        return false;
    }
    if (type != PHOTOC_FS_FILE) {
        return true;
    }
    ++context->result->files_visited;
    photoc_progress_update(context->progress,
                           (size_t)context->result->files_visited);
    uint64_t size;
    if (photoc_fs_file_size(path, &size) != 0) {
        warn_file(context, path, errno);
        return true;
    }
    if (context->count == context->capacity) {
        size_t capacity = context->capacity == 0 ? 16 : context->capacity * 2;
        if (capacity < context->capacity ||
            capacity > SIZE_MAX / sizeof(*context->files)) {
            context->error = EOVERFLOW;
            return false;
        }
        file_record *files = realloc(context->files, capacity * sizeof(*files));
        if (files == NULL) {
            context->error = errno;
            return false;
        }
        context->files = files;
        context->capacity = capacity;
    }
    char *copy = path_arena_copy(&context->paths, path);
    if (copy == NULL) {
        context->error = errno;
        return false;
    }
    context->files[context->count++] =
        (file_record){.path = copy, .size = size};
    return true;
}

static int compare_size(const void *left, const void *right)
{
    const file_record *a = left;
    const file_record *b = right;
    if (a->size != b->size) {
        return a->size < b->size ? -1 : 1;
    }
    return strcmp(a->path, b->path);
}

static int compare_digest(const void *left, const void *right)
{
    const file_record *a = left;
    const file_record *b = right;
    if (a->hashed != b->hashed) {
        return a->hashed ? -1 : 1;
    }
    if (!a->hashed) {
        return strcmp(a->path, b->path);
    }
    if (a->size != b->size) {
        return a->size < b->size ? -1 : 1;
    }
    int comparison = memcmp(a->digest, b->digest, sizeof(a->digest));
    return comparison != 0 ? comparison : strcmp(a->path, b->path);
}

static int compare_prefix_entry(const void *left, const void *right)
{
    const prefix_entry *a = left;
    const prefix_entry *b = right;
    size_t length = a->length < b->length ? a->length : b->length;
    int comparison = memcmp(a->prefix, b->prefix, length);
    if (comparison != 0) {
        return comparison;
    }
    if (a->length != b->length) {
        return a->length < b->length ? -1 : 1;
    }
    return strcmp(a->path, b->path);
}

static int read_file_prefix(const char *path, unsigned char *buffer,
                            size_t capacity, size_t *out_length)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }
    int flags = fcntl(fd, F_GETFL);
    if (flags >= 0) {
        (void)fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
    size_t total = 0;
    while (total < capacity) {
        ssize_t length = read(fd, buffer + total, capacity - total);
        if (length < 0 && errno == EINTR) {
            continue;
        }
        if (length < 0) {
            int saved_errno = errno;
            close(fd);
            errno = saved_errno;
            return -1;
        }
        if (length == 0) {
            break;
        }
        total += (size_t)length;
    }
    if (close(fd) != 0) {
        return -1;
    }
    *out_length = total;
    return 0;
}

static void mark_empty_hashes(find_context *context, size_t start, size_t end)
{
    for (size_t i = start; i < end; ++i) {
        file_record *file = &context->files[i];
        memcpy(file->digest, empty_sha256, sizeof(file->digest));
        file->hashed = true;
        ++context->result->files_hashed;
    }
}

static void mark_for_hashing(find_context *context, size_t start, size_t end)
{
    for (size_t i = start; i < end; ++i) {
        context->files[i].needs_hash = true;
    }
}

static void hash_size_group(find_context *context, size_t start, size_t end)
{
    uint64_t size = context->files[start].size;
    size_t count = end - start;
    if (count < 2) {
        return;
    }
    if (size == 0) {
        mark_empty_hashes(context, start, end);
        return;
    }

    size_t prefix_cap = size < DUPLICATE_PREFIX_BYTES
                            ? (size_t)size
                            : (size_t)DUPLICATE_PREFIX_BYTES;
    prefix_entry *entries = calloc(count, sizeof(*entries));
    unsigned char *prefix_bytes = malloc(count * prefix_cap);
    if (entries == NULL || prefix_bytes == NULL) {
        free(entries);
        free(prefix_bytes);
        mark_for_hashing(context, start, end);
        return;
    }

    size_t readable = 0;
    for (size_t i = 0; i < count; ++i) {
        file_record *file = &context->files[start + i];
        unsigned char *prefix = prefix_bytes + i * prefix_cap;
        size_t length = 0;
        if (read_file_prefix(file->path, prefix, prefix_cap, &length) != 0) {
            warn_file(context, file->path, errno);
            continue;
        }
        entries[readable++] = (prefix_entry){.index = start + i,
                                             .prefix = prefix,
                                             .length = length,
                                             .path = file->path};
    }

    if (readable > 1) {
        qsort(entries, readable, sizeof(*entries), compare_prefix_entry);
    }
    for (size_t group_start = 0; group_start < readable;) {
        size_t group_end = group_start + 1;
        while (group_end < readable &&
               entries[group_end].length == entries[group_start].length &&
               memcmp(entries[group_end].prefix, entries[group_start].prefix,
                      entries[group_start].length) == 0) {
            ++group_end;
        }
        if (group_end - group_start > 1) {
            for (size_t i = group_start; i < group_end; ++i) {
                file_record *file = &context->files[entries[i].index];
                /* Whole file already in the prefix buffer: hash in place. */
                if ((uint64_t)entries[i].length == file->size) {
                    int hash_status = photoc_hash_bytes_sha256(
                        entries[i].prefix, entries[i].length, file->digest);
                    if (hash_status != 0) {
                        warn_file(context, file->path, errno);
                    } else {
                        file->hashed = true;
                        ++context->result->files_hashed;
                    }
                } else {
                    file->needs_hash = true;
                }
            }
        }
        group_start = group_end;
    }

    free(prefix_bytes);
    free(entries);
}

static void hash_same_size_files(find_context *context)
{
    for (size_t start = 0; start < context->count;) {
        if (photoc_progress_interrupted())
            break;
        size_t end = start + 1;
        while (end < context->count &&
               context->files[end].size == context->files[start].size) {
            ++end;
        }
        hash_size_group(context, start, end);
        start = end;
    }
}

static void hash_one(file_record *file)
{
    if (photoc_hash_file_sha256(file->path, file->digest) != 0) {
        file->hash_error = errno;
    } else {
        file->hashed = true;
    }
}

typedef struct {
    file_record **files;
    atomic_size_t completed;
    atomic_bool cancel;
    photoc_progress *progress;
} hash_task_context;

static void hash_task(size_t index, void *user_data)
{
    hash_task_context *tasks = user_data;
    if (!atomic_load_explicit(&tasks->cancel, memory_order_relaxed))
        hash_one(tasks->files[index]);
    atomic_fetch_add_explicit(&tasks->completed, 1, memory_order_relaxed);
}

static void hash_poll(void *user_data)
{
    hash_task_context *tasks = user_data;
    if (photoc_progress_interrupted())
        atomic_store_explicit(&tasks->cancel, true, memory_order_relaxed);
    photoc_progress_update(
        tasks->progress,
        atomic_load_explicit(&tasks->completed, memory_order_relaxed));
}

static void hash_candidates(find_context *context, size_t workers)
{
    size_t count = 0;
    uint64_t bytes = 0;
    for (size_t i = 0; i < context->count; ++i) {
        if (context->files[i].needs_hash) {
            ++count;
            uint64_t size = context->files[i].size;
            bytes = size > UINT64_MAX - bytes ? UINT64_MAX : bytes + size;
        }
    }
    photoc_thread_pool *pool = NULL;
    file_record **jobs = NULL;
    photoc_progress_set_message(context->progress, "Hashing candidates...");
    photoc_progress_set_total(context->progress, count);
    /* Avoid thread startup and scheduling for tiny/mostly filtered corpora. */
    if (workers != 1 && count >= 4 && bytes >= UINT64_C(1048576) &&
        count <= SIZE_MAX / sizeof(*jobs)) {
        jobs = malloc(count * sizeof(*jobs));
        if (jobs != NULL)
            pool = photoc_thread_pool_create(workers);
    }
    if (pool != NULL) {
        size_t next = 0;
        for (size_t i = 0; i < context->count; ++i) {
            if (context->files[i].needs_hash)
                jobs[next++] = &context->files[i];
        }
        hash_task_context tasks = {.files = jobs,
                                   .progress = context->progress};
        atomic_init(&tasks.completed, 0);
        atomic_init(&tasks.cancel, false);
        if (photoc_thread_pool_run_poll(
                pool, count, hash_task, &tasks,
                context->progress == NULL ? NULL : hash_poll, &tasks) != 0) {
            for (size_t i = 0; i < count && !photoc_progress_interrupted(); ++i)
                hash_one(jobs[i]);
        }
        hash_poll(&tasks);
    } else {
        for (size_t i = 0; i < context->count; ++i) {
            if (photoc_progress_interrupted())
                break;
            if (context->files[i].needs_hash) {
                hash_one(&context->files[i]);
                photoc_progress_increment(context->progress);
            }
        }
    }
    photoc_thread_pool_destroy(pool);
    free(jobs);
    if (photoc_progress_interrupted())
        return;
    /* Warning callbacks and counters are serial and independent of completion
       order. No worker shares a digest buffer with another worker. */
    for (size_t i = 0; i < context->count; ++i) {
        file_record *file = &context->files[i];
        if (!file->needs_hash)
            continue;
        if (file->hashed)
            ++context->result->files_hashed;
        else
            warn_file(context, file->path, file->hash_error);
    }
}

static char *copy_path(const char *path)
{
    size_t length = strlen(path);
    if (length == SIZE_MAX) {
        errno = EOVERFLOW;
        return NULL;
    }
    char *copy = malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, path, length + 1);
    return copy;
}

static int append_group(find_context *context, size_t start, size_t count)
{
    photoc_duplicates_result *result = context->result;
    uint64_t size = context->files[start].size;
    if (count > UINT64_MAX - result->duplicate_files ||
        (size != 0 &&
         count - 1 > (UINT64_MAX - result->potential_savings) / size)) {
        errno = EOVERFLOW;
        return -1;
    }
    if (count > SIZE_MAX / sizeof(char *)) {
        errno = EOVERFLOW;
        return -1;
    }
    if (result->group_count == context->group_capacity) {
        size_t capacity =
            context->group_capacity == 0 ? 16 : context->group_capacity * 2;
        if (capacity < context->group_capacity ||
            capacity > SIZE_MAX / sizeof(*result->groups)) {
            errno = EOVERFLOW;
            return -1;
        }
        photoc_duplicate_group *groups =
            realloc(result->groups, capacity * sizeof(*groups));
        if (groups == NULL) {
            return -1;
        }
        result->groups = groups;
        context->group_capacity = capacity;
    }
    char **paths = malloc(count * sizeof(*paths));
    if (paths == NULL) {
        return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        paths[i] = NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        paths[i] = copy_path(context->files[start + i].path);
        if (paths[i] == NULL) {
            int saved_errno = errno;
            for (size_t j = 0; j < i; ++j) {
                free(paths[j]);
            }
            free(paths);
            errno = saved_errno;
            return -1;
        }
    }
    result->groups[result->group_count++] = (photoc_duplicate_group){
        .file_size = size, .paths = paths, .count = count};
    memcpy(result->groups[result->group_count - 1].sha256,
           context->files[start].digest, PHOTOC_SHA256_DIGEST_SIZE);
    result->duplicate_files += (uint64_t)count;
    result->potential_savings += (uint64_t)(count - 1) * size;
    return 0;
}

static int assemble_groups(find_context *context)
{
    if (context->count > 1) {
        qsort(context->files, context->count, sizeof(*context->files),
              compare_digest);
    }
    for (size_t start = 0;
         start < context->count && context->files[start].hashed;) {
        size_t end = start + 1;
        while (end < context->count && context->files[end].hashed &&
               context->files[end].size == context->files[start].size &&
               memcmp(context->files[end].digest, context->files[start].digest,
                      PHOTOC_SHA256_DIGEST_SIZE) == 0) {
            ++end;
        }
        if (end - start > 1 && append_group(context, start, end - start) != 0) {
            return -1;
        }
        start = end;
    }
    return 0;
}

static int find_with_progress(const char *directory, bool recursive,
                              size_t workers,
                              photoc_duplicates_warning_fn on_warning,
                              void *user_data, photoc_duplicates_result *result,
                              photoc_progress *progress)
{
    if (result == NULL || directory == NULL || directory[0] == '\0' ||
        workers > PHOTOC_MAX_WORKERS) {
        errno = EINVAL;
        return -1;
    }
    *result = (photoc_duplicates_result){0};
    find_context context = {.result = result,
                            .on_warning = on_warning,
                            .user_data = user_data,
                            .progress = progress};
    photoc_progress_set_message(progress, "Discovering files...");
    photoc_progress_start(progress);
    int walk_result =
        recursive ? photoc_fs_walk_recursive(directory, collect_file, &context)
                  : photoc_fs_walk(directory, collect_file, &context);
    int saved_errno = walk_result == 1 ? context.error : errno;
    if (walk_result == 0) {
        photoc_progress_set_message(progress,
                                    "Grouping duplicate candidates...");
        if (context.count > 1) {
            qsort(context.files, context.count, sizeof(*context.files),
                  compare_size);
        }
        hash_same_size_files(&context);
        if (!photoc_progress_interrupted())
            hash_candidates(&context, workers);
        if (photoc_progress_interrupted()) {
            saved_errno = EINTR;
            walk_result = -1;
        } else if (assemble_groups(&context) != 0) {
            saved_errno = errno;
            walk_result = -1;
        }
    }
    free(context.files);
    path_arena_cleanup(&context.paths);
    if (walk_result != 0) {
        photoc_duplicates_cleanup(result);
        errno = saved_errno;
        return -1;
    }
    return 0;
}

int photoc_duplicates_find_with_workers(const char *directory, bool recursive,
                                        size_t workers,
                                        photoc_duplicates_warning_fn on_warning,
                                        void *user_data,
                                        photoc_duplicates_result *result)
{
    return find_with_progress(directory, recursive, workers, on_warning,
                              user_data, result, NULL);
}

int photoc_duplicates_find_progress(const char *directory, bool recursive,
                                    photoc_duplicates_warning_fn on_warning,
                                    void *user_data,
                                    photoc_duplicates_result *result,
                                    photoc_progress *progress)
{
    return find_with_progress(directory, recursive, 0, on_warning, user_data,
                              result, progress);
}

int photoc_duplicates_find(const char *directory, bool recursive,
                           photoc_duplicates_warning_fn on_warning,
                           void *user_data, photoc_duplicates_result *result)
{
    return photoc_duplicates_find_with_workers(directory, recursive, 0,
                                               on_warning, user_data, result);
}
