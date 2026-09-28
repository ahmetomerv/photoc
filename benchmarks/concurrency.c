#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "photoc/duplicates.h"
#include "photoc/fs.h"
#include "photoc/scan.h"
#include "photoc/sharpness.h"
#include "photoc/thread_pool.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

typedef struct {
    char **paths;
    size_t count;
    size_t capacity;
    double *scores;
    photoc_image_result *errors;
    uint64_t checksum;
} benchmark;

static void checksum_bytes(benchmark *bench, const void *data, size_t length)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < length; ++i) {
        bench->checksum = (bench->checksum ^ bytes[i]) * UINT64_C(1099511628211);
    }
}

static bool collect_path(const char *path, photoc_fs_type type, void *user_data)
{
    benchmark *bench = user_data;
    if (type != PHOTOC_FS_FILE || !photoc_fs_is_jpeg(path)) return true;
    if (bench->count == bench->capacity) {
        size_t next = bench->capacity == 0 ? 32 : bench->capacity * 2;
        if (next < bench->capacity || next > SIZE_MAX / sizeof(*bench->paths)) {
            errno = EOVERFLOW;
            return false;
        }
        char **paths = realloc(bench->paths, next * sizeof(*paths));
        if (paths == NULL) return false;
        bench->paths = paths;
        bench->capacity = next;
    }
    char *copy = strdup(path);
    if (copy == NULL) return false;
    bench->paths[bench->count++] = copy;
    return true;
}

static bool observe_photo(const Photo *photo, void *user_data)
{
    benchmark *bench = user_data;
    ++bench->count;
    checksum_bytes(bench, photo->path, strlen(photo->path));
    checksum_bytes(bench, &photo->file_size, sizeof(photo->file_size));
    checksum_bytes(bench, &photo->iso, sizeof(photo->iso));
    return true;
}

static void focus_task(size_t index, void *user_data)
{
    benchmark *bench = user_data;
    bench->errors[index] = photoc_sharpness_score_jpeg(bench->paths[index],
                             PHOTOC_SHARPNESS_DEFAULT_MAX_DIMENSION,
                             &bench->scores[index]);
}

static int compare_paths(const void *left, const void *right)
{
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

static int benchmark_focus(const char *directory, size_t workers, benchmark *bench)
{
    if (photoc_fs_walk_recursive(directory, collect_path, bench) != 0) return -1;
    if (bench->count == 0) return 0;
    qsort(bench->paths, bench->count, sizeof(*bench->paths), compare_paths);
    bench->scores = calloc(bench->count, sizeof(*bench->scores));
    bench->errors = calloc(bench->count, sizeof(*bench->errors));
    if (bench->scores == NULL || bench->errors == NULL) return -1;
    photoc_thread_pool *pool = photoc_thread_pool_create(workers);
    if (pool == NULL) return -1;
    int result = photoc_thread_pool_run(pool, bench->count, focus_task, bench);
    photoc_thread_pool_destroy(pool);
    for (size_t i = 0; i < bench->count && result == 0; ++i) {
        if (bench->errors[i] != PHOTOC_IMAGE_OK) {
            fprintf(stderr, "%s: %s\n", bench->paths[i],
                    photoc_image_result_message(bench->errors[i]));
            result = -1;
        } else {
            checksum_bytes(bench, &bench->scores[i], sizeof(bench->scores[i]));
        }
    }
    return result;
}

static double now_seconds(void)
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (double)value.tv_sec + (double)value.tv_nsec / 1e9;
}

int main(int argc, char *argv[])
{
    if (argc != 4) {
        fputs("Usage: benchmark_concurrency metadata|duplicates|focus <directory> <workers>\n", stderr);
        return 2;
    }
    char *end = NULL;
    unsigned long parsed = strtoul(argv[3], &end, 10);
    if (argv[3][0] < '0' || argv[3][0] > '9' || *end != '\0' ||
        parsed > PHOTOC_MAX_WORKERS) return 2;
    size_t workers = (size_t)parsed;
    benchmark bench = {.checksum = UINT64_C(14695981039346656037)};
    int result;
    double start = now_seconds();
    if (strcmp(argv[1], "metadata") == 0) {
        photoc_scan_stats stats;
        result = photoc_scan_directory_with_workers(argv[2], true, workers,
                     observe_photo, NULL, &bench, &stats);
        if (result == 0 && stats.errors != 0) result = -1;
    } else if (strcmp(argv[1], "duplicates") == 0) {
        photoc_duplicates_result duplicates = {0};
        result = photoc_duplicates_find_with_workers(argv[2], true, workers,
                                                     NULL, NULL, &duplicates);
        bench.count = (size_t)duplicates.files_visited;
        checksum_bytes(&bench, &duplicates.files_hashed, sizeof(duplicates.files_hashed));
        for (size_t i = 0; i < duplicates.group_count; ++i) {
            photoc_duplicate_group *group = &duplicates.groups[i];
            checksum_bytes(&bench, group->sha256, sizeof(group->sha256));
            for (size_t j = 0; j < group->count; ++j) {
                checksum_bytes(&bench, group->paths[j], strlen(group->paths[j]));
            }
        }
        if (duplicates.errors != 0) result = -1;
        photoc_duplicates_cleanup(&duplicates);
    } else if (strcmp(argv[1], "focus") == 0) {
        result = benchmark_focus(argv[2], workers, &bench);
    } else {
        return 2;
    }
    double elapsed = now_seconds() - start;
    struct rusage usage = {0};
    getrusage(RUSAGE_SELF, &usage);
    if (result == 0) {
        printf("{\"items\":%zu,\"seconds\":%.9f,\"checksum\":\"%016" PRIx64
               "\",\"maxrss\":%ld}\n", bench.count, elapsed,
               bench.checksum, usage.ru_maxrss);
    } else {
        fputs("Benchmark failed\n", stderr);
    }
    if (bench.paths != NULL) {
        for (size_t i = 0; i < bench.count; ++i) free(bench.paths[i]);
    }
    free(bench.paths);
    free(bench.scores);
    free(bench.errors);
    return result == 0 ? 0 : 1;
}
