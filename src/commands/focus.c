#include "photoc/commands.h"

#include "photoc/error.h"
#include "photoc/exit_codes.h"
#include "photoc/fs.h"
#include "photoc/sharpness.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *path; /* Owned by this row; released by cleanup_walk. */
    double score;
    photoc_image_result result;
    int system_errno;
} focus_row;

typedef struct {
    focus_row *rows; /* Owns the array and every row's path. */
    size_t count;
    size_t capacity;
    size_t skipped;
    int error;
} focus_walk;

static void cleanup_walk(focus_walk *walk)
{
    for (size_t i = 0; i < walk->count; ++i) {
        free(walk->rows[i].path);
    }
    free(walk->rows);
}

/* The filesystem walker owns path only during this callback. Retain a copy
   for sorting, and decode one image at a time through the shared metric. */
static bool visit_file(const char *path, photoc_fs_type type, void *user_data)
{
    focus_walk *walk = user_data;
    if (type != PHOTOC_FS_FILE) {
        return true;
    }
    if (!photoc_fs_is_jpeg(path)) {
        ++walk->skipped;
        return true;
    }
    if (walk->count == walk->capacity) {
        size_t capacity = walk->capacity == 0 ? 32 : walk->capacity * 2;
        if (capacity < walk->capacity ||
            capacity > SIZE_MAX / sizeof(*walk->rows)) {
            walk->error = EOVERFLOW;
            return false;
        }
        focus_row *rows = realloc(walk->rows, capacity * sizeof(*rows));
        if (rows == NULL) {
            walk->error = ENOMEM;
            return false;
        }
        walk->rows = rows;
        walk->capacity = capacity;
    }
    size_t length = strlen(path);
    char *copy = malloc(length + 1);
    if (copy == NULL) {
        walk->error = ENOMEM;
        return false;
    }
    memcpy(copy, path, length + 1);
    focus_row *row = &walk->rows[walk->count++];
    *row = (focus_row){.path = copy};
    row->result = photoc_sharpness_score_jpeg(
        path, PHOTOC_SHARPNESS_DEFAULT_MAX_DIMENSION, &row->score);
    row->system_errno = errno;
    if (row->result == PHOTOC_IMAGE_NO_MEMORY) {
        walk->error = ENOMEM;
        return false;
    }
    return true;
}

static int compare_rows(const void *left, const void *right)
{
    const focus_row *a = left;
    const focus_row *b = right;
    bool a_ok = a->result == PHOTOC_IMAGE_OK;
    bool b_ok = b->result == PHOTOC_IMAGE_OK;
    if (a_ok != b_ok) {
        return a_ok ? -1 : 1;
    }
    if (a_ok && a->score != b->score) {
        return a->score < b->score ? -1 : 1;
    }
    return strcmp(a->path, b->path);
}

static int print_report(const char *path, bool directory, double threshold,
                        bool only_blurry, const focus_walk *walk)
{
    size_t analyzed = 0;
    size_t blurry = 0;
    size_t failed = 0;
    double mean = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;

    puts("JPEG sharpness (lowest scores first)");
    puts("Filename\tSharpness score");
    for (size_t i = 0; i < walk->count; ++i) {
        const focus_row *row = &walk->rows[i];
        const char *name =
            directory ? photoc_fs_relative(path, row->path) : row->path;
        if (row->result != PHOTOC_IMAGE_OK) {
            ++failed;
            photoc_error_image("focus",
                               directory ? PHOTOC_ERR_NOTE_WARNING
                                         : PHOTOC_ERR_NOTE_NONE,
                               name, row->result, row->system_errno);
            continue;
        }
        ++analyzed;
        mean += (row->score - mean) / (double)analyzed;
        if (analyzed == 1) {
            minimum = row->score;
        }
        maximum = row->score;
        bool possibly_blurry = row->score < threshold;
        if (possibly_blurry) {
            ++blurry;
        }
        if (!only_blurry || possibly_blurry) {
            printf("%s\t%.3f%s\n", name, row->score,
                   possibly_blurry ? "\tpossibly blurry" : "");
        }
    }
    printf("\nPhotos analyzed: %zu\n", analyzed);
    printf("Possibly blurry: %zu (score < %.6g)\n", blurry, threshold);
    printf("Files skipped: %zu\nFiles failed: %zu\n", walk->skipped, failed);
    if (analyzed == 0) {
        puts("Minimum score: Unavailable\nAverage score: Unavailable\n"
             "Maximum score: Unavailable");
    } else {
        printf("Minimum score: %.3f\nAverage score: %.3f\nMaximum score: "
               "%.3f\n",
               minimum, mean, maximum);
    }
    puts("Scores are a review aid, not proof of blur or artistic quality.");
    return failed == 0 ? PHOTOC_EXIT_SUCCESS : PHOTOC_EXIT_FAILURE;
}

int photoc_command_focus(const char *path, bool recursive, double threshold,
                         bool only_blurry)
{
    if (!isfinite(threshold) || threshold < 0.0) {
        return photoc_error_report(
            "focus", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_USAGE, NULL,
            "threshold must be finite and nonnegative", 0);
    }
    photoc_fs_type type;
    if (photoc_fs_get_type(path, &type) != 0) {
        return photoc_error_report("focus", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                   path, "unable to inspect path", errno);
    }
    if (type == PHOTOC_FS_OTHER ||
        (type == PHOTOC_FS_FILE && !photoc_fs_is_jpeg(path))) {
        return photoc_error_report(
            "focus", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_UNSUPPORTED, path,
            "expected a regular JPEG file or directory", 0);
    }
    if (type == PHOTOC_FS_FILE && recursive) {
        fputs("photoc focus: --recursive requires a directory\n", stderr);
        return PHOTOC_EXIT_USAGE;
    }

    focus_walk walk = {0};
    int result;
    if (type == PHOTOC_FS_FILE) {
        result = visit_file(path, type, &walk) ? 0 : 1;
    } else {
        result = recursive ? photoc_fs_walk_recursive(path, visit_file, &walk)
                           : photoc_fs_walk(path, visit_file, &walk);
    }
    if (result != 0) {
        int saved_errno = walk.error != 0 ? walk.error : errno;
        cleanup_walk(&walk);
        return photoc_error_report(
            "focus", PHOTOC_ERR_NOTE_NONE,
            walk.error != 0 ? PHOTOC_ERR_INTERNAL : PHOTOC_ERR_IO, path,
            walk.error != 0 ? "unable to retain analysis results"
                            : "unable to read directory",
            saved_errno);
    }
    if (walk.count > 1) {
        qsort(walk.rows, walk.count, sizeof(*walk.rows), compare_rows);
    }
    int exit_code = print_report(path, type == PHOTOC_FS_DIRECTORY, threshold,
                                 only_blurry, &walk);
    cleanup_walk(&walk);
    return exit_code;
}
