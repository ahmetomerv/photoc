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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s (errno=%d)\n", __FILE__, __LINE__,      \
                    #condition, errno);                                        \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

enum { PHOTO_COUNT = 72, HASH_COUNT = 16, EVENT_CAPACITY = 100 };
typedef struct {
    pthread_t caller;
    char *paths[EVENT_CAPACITY];
    uint32_t iso[EVENT_CAPACITY];
    bool warnings[EVENT_CAPACITY];
    int system_errors[EVENT_CAPACITY];
    size_t count;
    size_t photos;
    size_t stop_after;
    bool wrong_thread;
} observation;

static void record_event(observation *seen, const char *path, bool warning,
                         uint32_t iso)
{
    if (!pthread_equal(pthread_self(), seen->caller))
        seen->wrong_thread = true;
    CHECK(seen->count < EVENT_CAPACITY);
    if (seen->count == EVENT_CAPACITY)
        return;
    size_t i = seen->count++;
    seen->paths[i] = strdup(path);
    CHECK(seen->paths[i] != NULL);
    seen->warnings[i] = warning;
    seen->iso[i] = iso;
}

static bool observe_photo(const Photo *photo, void *user_data)
{
    observation *seen = user_data;
    record_event(seen, photo->path, false, photo->iso);
    ++seen->photos;
    return seen->stop_after == 0 || seen->photos < seen->stop_after;
}

static void observe_warning(const char *path, photoc_metadata_result reason,
                            int system_errno, void *user_data)
{
    CHECK((reason == PHOTOC_METADATA_INVALID_JPEG && system_errno == 0) ||
          (reason == PHOTOC_METADATA_IO_ERROR && system_errno == EACCES));
    observation *seen = user_data;
    record_event(seen, path, true, (uint32_t)reason);
    seen->system_errors[seen->count - 1] = system_errno;
}

static void clear_observation(observation *seen)
{
    for (size_t i = 0; i < seen->count; ++i)
        free(seen->paths[i]);
}

static void test_scan(const char *directory, size_t stop_after)
{
    observation serial = {.caller = pthread_self(), .stop_after = stop_after};
    photoc_scan_stats baseline = {0};
    int expected = stop_after == 0 ? 0 : 1;
    CHECK(photoc_scan_directory_with_workers(directory, false, 1, observe_photo,
                                             observe_warning, &serial,
                                             &baseline) == expected);
    if (stop_after == 0) {
        CHECK(baseline.files_visited == PHOTO_COUNT + HASH_COUNT);
        CHECK(baseline.jpeg_files_found == PHOTO_COUNT);
        CHECK(baseline.errors == (geteuid() == 0 ? 8u : 9u));
        CHECK(baseline.photos_parsed == PHOTO_COUNT - baseline.errors);
    }
    for (size_t workers = 2; workers <= PHOTOC_MAX_WORKERS; workers *= 2) {
        observation parallel = {.caller = pthread_self(),
                                .stop_after = stop_after};
        photoc_scan_stats stats = {0};
        CHECK(photoc_scan_directory_with_workers(
                  directory, false, workers, observe_photo, observe_warning,
                  &parallel, &stats) == expected);
        CHECK(!parallel.wrong_thread && !serial.wrong_thread);
        CHECK(stats.files_visited == baseline.files_visited);
        CHECK(stats.jpeg_files_found == baseline.jpeg_files_found);
        CHECK(stats.photos_parsed == baseline.photos_parsed);
        CHECK(stats.skipped_files == baseline.skipped_files);
        CHECK(stats.errors == baseline.errors);
        CHECK(parallel.count == serial.count);
        for (size_t i = 0; i < parallel.count && i < serial.count; ++i) {
            CHECK(strcmp(parallel.paths[i], serial.paths[i]) == 0);
            CHECK(parallel.warnings[i] == serial.warnings[i]);
            CHECK(parallel.iso[i] == serial.iso[i]);
            CHECK(parallel.system_errors[i] == serial.system_errors[i]);
        }
        clear_observation(&parallel);
    }
    clear_observation(&serial);
}

static void duplicate_warning(const char *path, int system_errno,
                              void *user_data)
{
    CHECK(system_errno == EACCES);
    record_event(user_data, path, true, (uint32_t)system_errno);
}

static void test_duplicates(const char *directory)
{
    photoc_duplicates_result serial = {0};
    observation serial_warnings = {.caller = pthread_self()};
    CHECK(photoc_duplicates_find_with_workers(directory, false, 1,
                                              duplicate_warning,
                                              &serial_warnings, &serial) == 0);
    for (size_t workers = 0; workers <= 4; ++workers) {
        photoc_duplicates_result parallel = {0};
        observation warnings = {.caller = pthread_self()};
        CHECK(photoc_duplicates_find_with_workers(directory, false, workers,
                                                  duplicate_warning, &warnings,
                                                  &parallel) == 0);
        CHECK(!warnings.wrong_thread &&
              warnings.count == serial_warnings.count);
        for (size_t i = 0; i < warnings.count && i < serial_warnings.count;
             ++i) {
            CHECK(strcmp(warnings.paths[i], serial_warnings.paths[i]) == 0);
            CHECK(warnings.iso[i] == serial_warnings.iso[i]);
        }
        CHECK(parallel.files_visited == serial.files_visited);
        CHECK(parallel.files_hashed == serial.files_hashed);
        CHECK(parallel.files_skipped == serial.files_skipped);
        CHECK(parallel.errors == serial.errors);
        CHECK(parallel.group_count == serial.group_count);
        CHECK(parallel.duplicate_files == serial.duplicate_files);
        CHECK(parallel.potential_savings == serial.potential_savings);
        for (size_t i = 0; i < parallel.group_count && i < serial.group_count;
             ++i) {
            photoc_duplicate_group *a = &parallel.groups[i],
                                   *b = &serial.groups[i];
            CHECK(a->file_size == b->file_size && a->count == b->count);
            CHECK(memcmp(a->sha256, b->sha256, sizeof(a->sha256)) == 0);
            for (size_t j = 0; j < a->count && j < b->count; ++j) {
                CHECK(strcmp(a->paths[j], b->paths[j]) == 0);
            }
        }
        photoc_duplicates_cleanup(&parallel);
        clear_observation(&warnings);
    }
    photoc_duplicates_cleanup(&serial);
    clear_observation(&serial_warnings);
}

enum { FOCUS_TASK_COUNT = 24, FOCUS_FIXTURE_COUNT = 6 };
static const char *const focus_paths[FOCUS_FIXTURE_COUNT] = {
    PHOTOC_CONCURRENCY_FIXTURES "/sharp.jpg",
    PHOTOC_CONCURRENCY_FIXTURES "/blurred.jpg",
    PHOTOC_CONCURRENCY_FIXTURES "/flat.jpg",
    PHOTOC_CONCURRENCY_FIXTURES "/large_sharp.jpg",
    PHOTOC_CONCURRENCY_FIXTURES "/invalid.jpg",
    PHOTOC_CONCURRENCY_FIXTURES "/missing.jpg"};
typedef struct {
    double scores[FOCUS_TASK_COUNT];
    photoc_image_result results[FOCUS_TASK_COUNT];
} focus_results;

static void score_task(size_t index, void *user_data)
{
    focus_results *results = user_data;
    results->scores[index] = -1.0;
    results->results[index] =
        photoc_sharpness_score_jpeg(focus_paths[index % FOCUS_FIXTURE_COUNT],
                                    1024, &results->scores[index]);
}

static void test_focus(void)
{
    focus_results serial = {0}, parallel = {0};
    for (size_t i = 0; i < FOCUS_TASK_COUNT; ++i)
        score_task(i, &serial);
    photoc_thread_pool *pool = photoc_thread_pool_create(2);
    CHECK(pool != NULL);
    if (pool == NULL)
        return;
    CHECK(photoc_thread_pool_run(pool, FOCUS_TASK_COUNT, score_task,
                                 &parallel) == 0);
    photoc_thread_pool_destroy(pool);
    for (size_t i = 0; i < FOCUS_TASK_COUNT; ++i) {
        CHECK(parallel.results[i] == serial.results[i]);
        CHECK(parallel.scores[i] == serial.scores[i]);
    }
}

static bool remove_file(const char *path, photoc_fs_type type, void *user_data)
{
    (void)user_data;
    if (type == PHOTOC_FS_FILE)
        CHECK(unlink(path) == 0);
    return true;
}

static int make_corpus(const char *directory)
{
    FILE *fixture = fopen(PHOTOC_CONCURRENCY_FIXTURES "/with_exif.jpg", "rb");
    if (fixture == NULL)
        return -1;
    unsigned char jpeg[4096];
    size_t length = fread(jpeg, 1, sizeof(jpeg), fixture);
    int read_error = ferror(fixture);
    if (fclose(fixture) != 0 || read_error)
        return -1;
    for (size_t i = 0; i < PHOTO_COUNT + HASH_COUNT; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "%03zu.%s", i,
                 i < PHOTO_COUNT ? "jpg" : "bin");
        char *path = NULL;
        if (photoc_fs_join(directory, name, &path) != 0)
            return -1;
        FILE *file = fopen(path, "wb");
        free(path);
        if (file == NULL)
            return -1;
        bool ok = true;
        if (i < PHOTO_COUNT) {
            size_t n = i % 9 == 0 ? 4 : length;
            ok = fwrite(jpeg, 1, n, file) == n;
        } else {
            unsigned char block[4096] = {0};
            for (size_t j = 0; j < 64 && ok; ++j) {
                /* Four identical files per digest, all sharing a prefix. */
                if (j == 63)
                    block[4095] = (unsigned char)((i - PHOTO_COUNT) / 4);
                ok = fwrite(block, 1, sizeof(block), file) == sizeof(block);
            }
        }
        if (fclose(file) != 0 || !ok)
            return -1;
    }
    return 0;
}

int main(void)
{
    const char *temporary = getenv("TMPDIR");
    if (temporary == NULL || temporary[0] == '\0')
        temporary = "/tmp";
    char *directory = NULL;
    CHECK(photoc_fs_join(temporary, "photoc-concurrency-XXXXXX", &directory) ==
          0);
    if (directory == NULL || mkdtemp(directory) == NULL)
        return 1;
    CHECK(make_corpus(directory) == 0);
    char *unreadable = NULL;
    CHECK(photoc_fs_join(directory, "071.jpg", &unreadable) == 0);
    if (unreadable != NULL && geteuid() != 0)
        CHECK(chmod(unreadable, 0000) == 0);
    test_scan(directory, 0);
    test_scan(directory, 5);
    test_scan(directory, 33); /* Stop across a batch boundary. */
    test_duplicates(directory);
    test_focus();
    photoc_scan_stats stats;
    errno = 0;
    CHECK(photoc_scan_directory_with_workers(
              directory, false, PHOTOC_MAX_WORKERS + 1, observe_photo, NULL,
              NULL, &stats) == -1 &&
          errno == EINVAL);
    photoc_duplicates_result result;
    CHECK(photoc_duplicates_find_with_workers(directory, false,
                                              PHOTOC_MAX_WORKERS + 1, NULL,
                                              NULL, &result) == -1 &&
          errno == EINVAL);
    free(unreadable);
    CHECK(photoc_fs_walk(directory, remove_file, NULL) == 0);
    CHECK(rmdir(directory) == 0);
    free(directory);
    return failures == 0 ? 0 : 1;
}
