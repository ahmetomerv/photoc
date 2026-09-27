#include "photoc/duplicates.h"

#include "photoc/fs.h"
#include "photoc/hash.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *path;
    uint64_t size;
    unsigned char digest[PHOTOC_SHA256_DIGEST_SIZE];
    bool hashed;
} file_record;

typedef struct {
    file_record *files;
    size_t count;
    size_t capacity;
    size_t group_capacity;
    int error;
    photoc_duplicates_result *result;
    photoc_duplicates_warning_fn on_warning;
    void *user_data;
} find_context;

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

static void warn_file(find_context *context, const char *path, int error)
{
    ++context->result->files_skipped;
    ++context->result->errors;
    if (context->on_warning != NULL) {
        context->on_warning(path, error, context->user_data);
    }
}

static bool collect_file(const char *path, photoc_fs_type type, void *user_data)
{
    find_context *context = user_data;
    if (type != PHOTOC_FS_FILE) {
        return true;
    }
    ++context->result->files_visited;
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
        file_record *files = realloc(context->files,
                                     capacity * sizeof(*files));
        if (files == NULL) {
            context->error = errno;
            return false;
        }
        context->files = files;
        context->capacity = capacity;
    }
    size_t length = strlen(path);
    char *copy = malloc(length + 1);
    if (copy == NULL) {
        context->error = errno;
        return false;
    }
    memcpy(copy, path, length + 1);
    context->files[context->count++] = (file_record){.path = copy, .size = size};
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

static void hash_same_size_files(find_context *context)
{
    for (size_t start = 0; start < context->count;) {
        size_t end = start + 1;
        while (end < context->count &&
               context->files[end].size == context->files[start].size) {
            ++end;
        }
        if (end - start > 1) {
            for (size_t i = start; i < end; ++i) {
                file_record *file = &context->files[i];
                if (photoc_hash_file_sha256(file->path, file->digest) != 0) {
                    warn_file(context, file->path, errno);
                } else {
                    file->hashed = true;
                    ++context->result->files_hashed;
                }
            }
        }
        start = end;
    }
}

static int append_group(find_context *context, size_t start, size_t count)
{
    photoc_duplicates_result *result = context->result;
    uint64_t size = context->files[start].size;
    if (count > UINT64_MAX - result->duplicate_files ||
        (size != 0 && count - 1 >
         (UINT64_MAX - result->potential_savings) / size)) {
        errno = EOVERFLOW;
        return -1;
    }
    if (count > SIZE_MAX / sizeof(char *)) {
        errno = EOVERFLOW;
        return -1;
    }
    if (result->group_count == context->group_capacity) {
        size_t capacity = context->group_capacity == 0 ?
                          16 : context->group_capacity * 2;
        if (capacity < context->group_capacity ||
            capacity > SIZE_MAX / sizeof(*result->groups)) {
            errno = EOVERFLOW;
            return -1;
        }
        photoc_duplicate_group *groups = realloc(
            result->groups, capacity * sizeof(*groups));
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
        paths[i] = context->files[start + i].path;
        context->files[start + i].path = NULL;
    }
    result->groups[result->group_count++] = (photoc_duplicate_group){
        .file_size = size, .paths = paths, .count = count
    };
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
    for (size_t start = 0; start < context->count &&
                           context->files[start].hashed;) {
        size_t end = start + 1;
        while (end < context->count && context->files[end].hashed &&
               context->files[end].size == context->files[start].size &&
               memcmp(context->files[end].digest,
                      context->files[start].digest,
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

int photoc_duplicates_find(const char *directory, bool recursive,
                           photoc_duplicates_warning_fn on_warning,
                           void *user_data, photoc_duplicates_result *result)
{
    if (result == NULL || directory == NULL || directory[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    *result = (photoc_duplicates_result){0};
    find_context context = {
        .result = result, .on_warning = on_warning, .user_data = user_data
    };
    int walk_result = recursive ?
        photoc_fs_walk_recursive(directory, collect_file, &context) :
        photoc_fs_walk(directory, collect_file, &context);
    int saved_errno = walk_result == 1 ? context.error : errno;
    if (walk_result == 0) {
        if (context.count > 1) {
            qsort(context.files, context.count, sizeof(*context.files),
                  compare_size);
        }
        hash_same_size_files(&context);
        if (assemble_groups(&context) != 0) {
            saved_errno = errno;
            walk_result = -1;
        }
    }
    for (size_t i = 0; i < context.count; ++i) {
        free(context.files[i].path);
    }
    free(context.files);
    if (walk_result != 0) {
        photoc_duplicates_cleanup(result);
        errno = saved_errno;
        return -1;
    }
    return 0;
}
