#define _POSIX_C_SOURCE 200809L

#include "photoc/commands.h"

#include "photoc/error.h"
#include "photoc/exit_codes.h"
#include "photoc/fs.h"
#include "photoc/jpeg_check.h"
#include "photoc/json.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *path; /* Owned by this row. */
    photoc_check_result result;
} check_row;

typedef struct {
    check_row *rows; /* Owns the array and all path copies. */
    size_t count;
    size_t capacity;
    size_t skipped;
    int error;
    const photoc_output *output; /* Borrowed during collection. */
    photoc_progress *progress; /* Borrowed; caller thread only. */
} check_walk;

typedef struct {
    size_t ok;
    size_t warnings;
    size_t errors;
} check_summary;

static void cleanup_walk(check_walk *walk)
{
    for (size_t i = 0; i < walk->count; ++i) {
        free(walk->rows[i].path);
    }
    free(walk->rows);
}

static bool collect_file(const char *path, photoc_fs_type type, void *user_data)
{
    check_walk *walk = user_data;
    if (photoc_progress_interrupted()) {
        walk->error = EINTR;
        return false;
    }
    if (type != PHOTOC_FS_FILE) {
        return true;
    }
    if (!photoc_fs_is_jpeg(path)) {
        ++walk->skipped;
        photoc_output_verbose(walk->output, "check",
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
        check_row *rows = realloc(walk->rows, capacity * sizeof(*rows));
        if (rows == NULL) {
            walk->error = ENOMEM;
            return false;
        }
        walk->rows = rows;
        walk->capacity = capacity;
    }
    char *copy = strdup(path);
    if (copy == NULL) {
        walk->error = ENOMEM;
        return false;
    }
    walk->rows[walk->count++] = (check_row){.path = copy};
    photoc_progress_update(walk->progress, walk->count);
    return true;
}

static int compare_rows(const void *left, const void *right)
{
    const check_row *a = left;
    const check_row *b = right;
    return strcmp(a->path, b->path);
}

static void report_error(const char *name, photoc_check_result result)
{
    photoc_error_kind kind = PHOTOC_ERR_DECODE;
    if (result.code == PHOTOC_CHECK_IO_ERROR ||
        result.code == PHOTOC_CHECK_FILE_CHANGED) {
        kind = PHOTOC_ERR_IO;
    } else if (result.code == PHOTOC_CHECK_UNSUPPORTED_JPEG) {
        kind = PHOTOC_ERR_UNSUPPORTED;
    } else if (result.code == PHOTOC_CHECK_RESOURCE_LIMIT ||
               result.code == PHOTOC_CHECK_INTERNAL_ERROR) {
        kind = PHOTOC_ERR_INTERNAL;
    }
    photoc_error_report("check", PHOTOC_ERR_NOTE_NONE, kind, name,
                        photoc_check_message(result), result.system_errno);
}

/* Keep control characters and backslashes from creating fake report rows. */
static void print_path(const char *path)
{
    for (const unsigned char *p = (const unsigned char *)path; *p != 0; ++p) {
        if (*p < 0x20 || *p == 0x7f) {
            printf("\\x%02x", (unsigned int)*p);
        } else if (*p == '\\') {
            fputs("\\\\", stdout);
        } else {
            fputc(*p, stdout);
        }
    }
}

static void print_report(const char *path, bool directory, bool only_errors,
                         const check_walk *walk, const check_summary *summary,
                         const photoc_output *output)
{
    for (size_t i = 0; i < walk->count; ++i) {
        const check_row *row = &walk->rows[i];
        if (only_errors && row->result.status != PHOTOC_CHECK_ERROR) {
            continue;
        }
        const char *status = row->result.status == PHOTOC_CHECK_OK ? "OK"
                             : row->result.status == PHOTOC_CHECK_WARNING
                                 ? "WARNING"
                                 : "ERROR";
        printf("%-7s  ", status);
        print_path(directory ? photoc_fs_relative(path, row->path) : row->path);
        if (row->result.status != PHOTOC_CHECK_OK) {
            printf("  %s", photoc_check_message(row->result));
        }
        putchar('\n');
    }
    /* These counts are requested audit results, retained in quiet mode. */
    printf("\nFiles checked: %zu\nOK: %zu\nWarnings: %zu\nErrors: %zu\n",
           walk->count, summary->ok, summary->warnings, summary->errors);
    photoc_output_info(
        output, "Checks cover structural readability, not visual quality "
                "or complete metadata validity.\n");
}

static int print_json(const char *path, bool directory, bool only_errors,
                      const check_walk *walk, const check_summary *summary)
{
    printf("{\n  \"summary\": {\"files_checked\": %zu, \"ok\": %zu, "
           "\"warnings\": %zu, \"errors\": %zu, \"files_skipped\": %zu},\n"
           "  \"files\": [",
           walk->count, summary->ok, summary->warnings, summary->errors,
           walk->skipped);
    bool first = true;
    for (size_t i = 0; i < walk->count; ++i) {
        const check_row *row = &walk->rows[i];
        if (only_errors && row->result.status != PHOTOC_CHECK_ERROR) {
            continue;
        }
        printf("%s\n    {\"path\": ", first ? "" : ",");
        if (photoc_json_write_string(
                stdout, directory ? photoc_fs_relative(path, row->path)
                                  : row->path) != 0) {
            return -1;
        }
        printf(", \"status\": \"%s\", \"code\": \"%s\", \"message\": ",
               photoc_check_status_name(row->result.status),
               photoc_check_code_name(row->result.code));
        if (photoc_json_write_string(stdout,
                                     photoc_check_message(row->result)) != 0) {
            return -1;
        }
        putchar('}');
        first = false;
    }
    fputs(first ? "]\n}\n" : "\n  ]\n}\n", stdout);
    return ferror(stdout) ? -1 : 0;
}

int photoc_command_check_with_output(const char *path, bool recursive,
                                     bool only_errors, bool json,
                                     const photoc_output *output)
{
    photoc_fs_type type;
    if (photoc_fs_get_type(path, &type) != 0) {
        return photoc_error_report("check", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                   path, "unable to inspect path", errno);
    }
    if (type == PHOTOC_FS_OTHER ||
        (type == PHOTOC_FS_FILE && !photoc_fs_is_jpeg(path))) {
        return photoc_error_report(
            "check", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_UNSUPPORTED, path,
            "expected a regular JPEG file or directory", 0);
    }
    if (type == PHOTOC_FS_FILE && recursive) {
        return photoc_error_report("check", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_USAGE, NULL,
                                   "--recursive requires a directory", 0);
    }
    photoc_output_verbose(output, "check",
                          "input: %s; mode: full decode audit; recursive: %s; "
                          "only errors: %s; output: %s; workers: 1\n",
                          path, recursive ? "yes" : "no",
                          only_errors ? "yes" : "no", json ? "JSON" : "human");
    photoc_progress *progress = output == NULL ? NULL : output->progress;
    check_walk walk = {.output = output,
                       .progress = type == PHOTOC_FS_DIRECTORY ? progress : NULL};
    if (type == PHOTOC_FS_DIRECTORY) {
        photoc_progress_set_message(progress, "Discovering photos...");
        photoc_progress_start(progress);
    }
    int collected =
        type == PHOTOC_FS_FILE
            ? (collect_file(path, type, &walk) ? 0 : 1)
            : (recursive ? photoc_fs_walk_recursive(path, collect_file, &walk)
                         : photoc_fs_walk(path, collect_file, &walk));
    if (collected != 0) {
        photoc_progress_fail(progress, "Failed to scan directory");
        int saved_errno = walk.error == 0 ? errno : walk.error;
        cleanup_walk(&walk);
        return photoc_error_report(
            "check", PHOTOC_ERR_NOTE_NONE,
            walk.error == 0 ? PHOTOC_ERR_IO : PHOTOC_ERR_INTERNAL, path,
            walk.error == 0 ? "unable to read directory"
                            : "unable to retain audit paths",
            saved_errno);
    }
    if (walk.count > 1) {
        qsort(walk.rows, walk.count, sizeof(*walk.rows), compare_rows);
    }
    if (type == PHOTOC_FS_DIRECTORY) {
        photoc_progress_set_message(progress, "Checking JPEGs...");
        if (progress != NULL && progress->enabled)
            photoc_progress_set_total(progress, walk.count);
    }
    check_summary summary = {0};
    bool directory = type == PHOTOC_FS_DIRECTORY;
    /* One full decoder at a time bounds coefficient memory without multiplying
       it by the shared pool's worker count. Only paths/results are retained. */
    for (size_t i = 0; i < walk.count; ++i) {
        if (photoc_progress_interrupted())
            break;
        check_row *row = &walk.rows[i];
        row->result = photoc_jpeg_check_file(row->path);
        switch (row->result.status) {
        case PHOTOC_CHECK_OK:
            ++summary.ok;
            break;
        case PHOTOC_CHECK_WARNING:
            ++summary.warnings;
            break;
        case PHOTOC_CHECK_ERROR:
            ++summary.errors;
            photoc_progress_before_diagnostic(progress);
            report_error(directory ? photoc_fs_relative(path, row->path)
                                   : row->path,
                         row->result);
            break;
        }
        photoc_progress_increment(walk.progress);
    }
    if (photoc_progress_interrupted()) {
        photoc_progress_warn(progress, "Interrupted");
        cleanup_walk(&walk);
        return PHOTOC_EXIT_FAILURE;
    }
    if (directory) {
        char message[96];
        if (summary.errors != 0) {
            snprintf(message, sizeof(message),
                     "Checked %zu JPEGs; %zu errors", walk.count,
                     summary.errors);
            photoc_progress_warn(progress, message);
        } else {
            snprintf(message, sizeof(message), "Checked %zu JPEGs",
                     walk.count);
            photoc_progress_finish(progress, message);
        }
    }
    photoc_output_verbose(
        output, "check",
        "JPEG files discovered: %zu; checked: %zu; skipped: %zu; "
        "warnings: %zu; errors: %zu\n",
        walk.count, walk.count, walk.skipped, summary.warnings, summary.errors);
    int output_result;
    if (json) {
        output_result =
            print_json(path, directory, only_errors, &walk, &summary);
    } else {
        print_report(path, directory, only_errors, &walk, &summary, output);
        output_result = ferror(stdout) ? -1 : 0;
    }
    int saved_errno = errno;
    cleanup_walk(&walk);
    if (output_result != 0) {
        return photoc_error_report("check", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                   NULL, "unable to write output",
                                   saved_errno == 0 ? EIO : saved_errno);
    }
    return summary.errors == 0 ? PHOTOC_EXIT_SUCCESS : PHOTOC_EXIT_FAILURE;
}

int photoc_command_check(const char *path, bool recursive, bool only_errors,
                         bool json)
{
    return photoc_command_check_with_output(path, recursive, only_errors, json,
                                            NULL);
}
