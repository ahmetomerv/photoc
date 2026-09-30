#include "photoc/commands.h"

#include "photoc/error.h"
#include "photoc/exit_codes.h"
#include "photoc/format.h"
#include "photoc/fs.h"
#include "photoc/json.h"
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
    const photoc_output *output; /* Borrowed during traversal. */
    photoc_progress *progress; /* Borrowed; caller thread only. */
} focus_walk;

typedef struct {
    size_t analyzed;
    size_t blurry;
    size_t skipped;
    size_t failed;
    double mean;
    double minimum;
    double maximum;
} focus_summary;

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
    if (photoc_progress_interrupted()) {
        walk->error = EINTR;
        return false;
    }
    if (type != PHOTOC_FS_FILE) {
        return true;
    }
    if (!photoc_fs_is_jpeg(path)) {
        ++walk->skipped;
        photoc_output_verbose(walk->output, "focus",
                              "skipped '%s': unsupported file extension\n",
                              path);
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
    photoc_progress_increment(walk->progress);
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

static focus_summary summarize_results(const focus_walk *walk, double threshold)
{
    focus_summary summary = {.skipped = walk->skipped};
    for (size_t i = 0; i < walk->count; ++i) {
        const focus_row *row = &walk->rows[i];
        if (row->result != PHOTOC_IMAGE_OK) {
            ++summary.failed;
            continue;
        }
        ++summary.analyzed;
        summary.mean += (row->score - summary.mean) / (double)summary.analyzed;
        if (summary.analyzed == 1) {
            summary.minimum = row->score;
        }
        summary.maximum = row->score;
        if (row->score < threshold) {
            ++summary.blurry;
        }
    }
    return summary;
}

static void report_errors(const char *path, bool directory,
                          const focus_walk *walk)
{
    /* Decode warnings cause exit status 1, so they are never filtered. */
    for (size_t i = 0; i < walk->count; ++i) {
        const focus_row *row = &walk->rows[i];
        if (row->result != PHOTOC_IMAGE_OK) {
            photoc_error_image(
                "focus",
                directory ? PHOTOC_ERR_NOTE_WARNING : PHOTOC_ERR_NOTE_NONE,
                directory ? photoc_fs_relative(path, row->path) : row->path,
                row->result, row->system_errno);
        }
    }
}

static void print_report(const char *path, bool directory, double threshold,
                         bool only_blurry, const focus_walk *walk,
                         const focus_summary *summary,
                         const photoc_output *output)
{
    puts("JPEG sharpness (lowest scores first)");
    puts("Filename\tSharpness score");
    for (size_t i = 0; i < walk->count; ++i) {
        const focus_row *row = &walk->rows[i];
        const char *name =
            directory ? photoc_fs_relative(path, row->path) : row->path;
        if (row->result != PHOTOC_IMAGE_OK) {
            continue;
        }
        bool possibly_blurry = row->score < threshold;
        if (!only_blurry || possibly_blurry) {
            printf("%s\t%.3f%s\n", name, row->score,
                   possibly_blurry ? "\tpossibly blurry" : "");
        }
    }
    printf("\nPhotos analyzed: %zu\n", summary->analyzed);
    printf("Possibly blurry: %zu (score < %.6g)\n", summary->blurry, threshold);
    photoc_output_info(output, "Files skipped: %zu\nFiles failed: %zu\n",
                       summary->skipped, summary->failed);
    if (summary->analyzed == 0) {
        puts("Minimum score: Unavailable\nAverage score: Unavailable\n"
             "Maximum score: Unavailable");
    } else {
        printf("Minimum score: %.3f\nAverage score: %.3f\nMaximum score: "
               "%.3f\n",
               summary->minimum, summary->mean, summary->maximum);
    }
    photoc_output_info(
        output,
        "Scores are a review aid, not proof of blur or artistic quality.\n");
}

static int print_json(const char *path, bool directory, bool recursive,
                      double threshold, bool only_blurry,
                      const focus_walk *walk, const focus_summary *summary)
{
    fputs("{\n  \"path\": ", stdout);
    if (photoc_json_write_string(stdout, path) != 0) {
        return -1;
    }
    printf(",\n  \"threshold\": %.17g,\n  \"recursive\": %s,\n"
           "  \"only_blurry\": %s,\n  \"photos\": [",
           threshold, recursive ? "true" : "false",
           only_blurry ? "true" : "false");
    bool first = true;
    for (size_t i = 0; i < walk->count; ++i) {
        const focus_row *row = &walk->rows[i];
        if (row->result != PHOTOC_IMAGE_OK) {
            continue;
        }
        bool possibly_blurry = row->score < threshold;
        if (only_blurry && !possibly_blurry) {
            continue;
        }
        printf("%s\n    {\"path\": ", first ? "" : ",");
        if (photoc_json_write_string(
                stdout, directory ? photoc_fs_relative(path, row->path)
                                  : row->path) != 0) {
            return -1;
        }
        /* Round-trip precision keeps score < threshold consistent with the
           serialized values, even close to the classification boundary. */
        printf(", \"score\": %.17g, \"threshold\": %.17g, "
               "\"possibly_blurry\": %s}",
               row->score, threshold, possibly_blurry ? "true" : "false");
        first = false;
    }
    fputs(first ? "]" : "\n  ]", stdout);
    printf(",\n  \"summary\": {\n    \"photos_analyzed\": %zu,\n"
           "    \"possibly_blurry\": %zu,\n    \"files_skipped\": %zu,\n"
           "    \"files_failed\": %zu,\n    \"minimum_score\": ",
           summary->analyzed, summary->blurry, summary->skipped,
           summary->failed);
    if (summary->analyzed == 0) {
        fputs("null,\n    \"average_score\": null,\n"
              "    \"maximum_score\": null",
              stdout);
    } else {
        printf("%.17g,\n    \"average_score\": %.17g,\n"
               "    \"maximum_score\": %.17g",
               summary->minimum, summary->mean, summary->maximum);
    }
    fputs("\n  }\n}\n", stdout);
    return ferror(stdout) ? -1 : 0;
}

int photoc_command_focus_with_output(const char *path, bool recursive,
                                     double threshold, bool only_blurry,
                                     bool json, const photoc_output *output)
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

    photoc_output_verbose(output, "focus",
                          "input: %s; mode: sharpness; recursive: %s; "
                          "threshold: %.6g; only blurry: %s; output: %s\n",
                          path, recursive ? "yes" : "no", threshold,
                          only_blurry ? "yes" : "no", json ? "JSON" : "human");
    photoc_progress *progress = output == NULL ? NULL : output->progress;
    focus_walk walk = {.output = output,
                       .progress = type == PHOTOC_FS_DIRECTORY ? progress : NULL};
    if (type == PHOTOC_FS_DIRECTORY) {
        size_t total = 0;
        photoc_progress_set_message(progress, "Discovering photos...");
        if (photoc_progress_discover(progress, path, recursive,
                                     PHOTOC_FORMATS_JPEG, &total) != 0) {
            photoc_progress_fail(progress, "Failed to scan directory");
            return photoc_error_report("focus", PHOTOC_ERR_NOTE_NONE,
                                       PHOTOC_ERR_IO, path,
                                       "unable to read directory", errno);
        }
        photoc_progress_set_message(progress, "Analyzing sharpness...");
        if (progress != NULL && progress->enabled)
            photoc_progress_set_total(progress, total);
    }
    int result;
    if (type == PHOTOC_FS_FILE) {
        result = visit_file(path, type, &walk) ? 0 : 1;
    } else {
        result = recursive ? photoc_fs_walk_recursive(path, visit_file, &walk)
                           : photoc_fs_walk(path, visit_file, &walk);
    }
    if (photoc_progress_interrupted())
        result = 1;
    if (result != 0) {
        photoc_progress_fail(progress, "Failed to analyze photos");
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
    focus_summary summary = summarize_results(&walk, threshold);
    if (type == PHOTOC_FS_DIRECTORY) {
        char message[96];
        if (summary.failed != 0) {
            snprintf(message, sizeof(message),
                     "Analyzed %zu photos; %zu could not be processed",
                     summary.analyzed, summary.failed);
            photoc_progress_warn(progress, message);
        } else {
            snprintf(message, sizeof(message), "Analyzed %zu photos",
                     summary.analyzed);
            photoc_progress_finish(progress, message);
        }
    }
    photoc_output_verbose(output, "focus",
                          "JPEG files discovered: %zu; analyzed: %zu; skipped: "
                          "%zu; decode failures: %zu\n",
                          walk.count, summary.analyzed, summary.skipped,
                          summary.failed);
    bool directory = type == PHOTOC_FS_DIRECTORY;
    report_errors(path, directory, &walk);
    int output_result = 0;
    if (json) {
        output_result = print_json(path, directory, recursive, threshold,
                                   only_blurry, &walk, &summary);
    } else {
        print_report(path, directory, threshold, only_blurry, &walk, &summary,
                     output);
        output_result = ferror(stdout) ? -1 : 0;
    }
    int saved_errno = errno;
    cleanup_walk(&walk);
    if (output_result != 0) {
        return photoc_error_report("focus", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                   NULL, "unable to write output",
                                   saved_errno == 0 ? EIO : saved_errno);
    }
    return summary.failed == 0 ? PHOTOC_EXIT_SUCCESS : PHOTOC_EXIT_FAILURE;
}

int photoc_command_focus(const char *path, bool recursive, double threshold,
                         bool only_blurry, bool json)
{
    return photoc_command_focus_with_output(path, recursive, threshold,
                                            only_blurry, json, NULL);
}
