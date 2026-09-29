#define _POSIX_C_SOURCE 200809L

#include "photoc/commands.h"

#include "photoc/error.h"
#include "photoc/exit_codes.h"
#include "photoc/fs.h"
#include "photoc/json.h"
#include "photoc/scan.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const photoc_query *query; /* Borrowed during scanning. */
    Photo *matches;            /* Owns array and each retained Photo. */
    size_t count;
    size_t capacity;
    int error;
} query_scan;

static void cleanup_scan(query_scan *scan)
{
    for (size_t i = 0; i < scan->count; ++i)
        photo_cleanup(&scan->matches[i]);
    free(scan->matches);
}

/* Retain only matches after the scanner's borrowed callback ends. Each string
   has a separate owned copy; scalar fields and presence flags copy by value. */
static bool copy_photo(const Photo *source, Photo *destination)
{
    Photo copy = *source;
    copy.path = NULL;
    copy.camera_make = NULL;
    copy.camera_model = NULL;
    copy.lens_model = NULL;
    copy.capture_timestamp = NULL;
    copy.path = strdup(source->path);
    if (source->camera_make != NULL)
        copy.camera_make = strdup(source->camera_make);
    if (source->camera_model != NULL)
        copy.camera_model = strdup(source->camera_model);
    if (source->lens_model != NULL)
        copy.lens_model = strdup(source->lens_model);
    if (source->capture_timestamp != NULL)
        copy.capture_timestamp = strdup(source->capture_timestamp);
    if (copy.path == NULL ||
        (source->camera_make != NULL && copy.camera_make == NULL) ||
        (source->camera_model != NULL && copy.camera_model == NULL) ||
        (source->lens_model != NULL && copy.lens_model == NULL) ||
        (source->capture_timestamp != NULL && copy.capture_timestamp == NULL)) {
        photo_cleanup(&copy);
        return false;
    }
    *destination = copy;
    return true;
}

static bool collect_match(const Photo *photo, void *user_data)
{
    query_scan *scan = user_data;
    if (!photoc_query_matches(scan->query, photo))
        return true;
    if (scan->count == scan->capacity) {
        size_t capacity = scan->capacity == 0 ? 32 : scan->capacity * 2;
        if (capacity < scan->capacity ||
            capacity > SIZE_MAX / sizeof(*scan->matches)) {
            scan->error = EOVERFLOW;
            return false;
        }
        Photo *matches = realloc(scan->matches, capacity * sizeof(*matches));
        if (matches == NULL) {
            scan->error = ENOMEM;
            return false;
        }
        scan->matches = matches;
        scan->capacity = capacity;
    }
    if (!copy_photo(photo, &scan->matches[scan->count])) {
        scan->error = ENOMEM;
        return false;
    }
    ++scan->count;
    return true;
}

static void scan_failure(const char *path, photoc_metadata_result result,
                         int system_errno, void *user_data)
{
    (void)user_data;
    /* A skipped JPEG makes the search incomplete and causes exit 1. */
    photoc_error_metadata("query", PHOTOC_ERR_NOTE_NONE, path, result,
                          system_errno);
}

static int compare_photos(const void *left, const void *right)
{
    const Photo *a = left;
    const Photo *b = right;
    return strcmp(a->path, b->path);
}

static void json_number(bool present, double value)
{
    if (present && isfinite(value))
        printf("%.17g", value);
    else
        fputs("null", stdout);
}

static void json_field(const char *name, const char *value)
{
    printf("\"%s\": ", name);
    photoc_json_write_string(stdout, value);
}

static void print_json(const char *path, bool recursive,
                       const photoc_query *query, const query_scan *scan,
                       const photoc_scan_stats *stats)
{
    const photoc_query_filters *filters = &query->filters;
    fputs("{\n  \"query\": {", stdout);
    json_field("path", path);
    printf(", \"recursive\": %s, \"filters\": {", recursive ? "true" : "false");
    json_field("camera", filters->camera);
    fputs(", ", stdout);
    json_field("make", filters->make);
    fputs(", ", stdout);
    json_field("iso", filters->iso);
    fputs(", ", stdout);
    json_field("aperture", filters->aperture);
    fputs(", ", stdout);
    json_field("focal", filters->focal);
    fputs(", ", stdout);
    json_field("after", filters->after);
    fputs(", ", stdout);
    json_field("before", filters->before);
    fputs(", ", stdout);
    json_field("gps", filters->has_gps  ? "present"
                      : filters->no_gps ? "absent"
                                        : NULL);
    fputs("}, \"string_matching\": \"exact_case_sensitive\", "
          "\"date_bounds\": \"inclusive\"},\n  \"matches\": [",
          stdout);
    for (size_t i = 0; i < scan->count; ++i) {
        const Photo *photo = &scan->matches[i];
        fputs(i == 0 ? "\n    {" : ",\n    {", stdout);
        json_field("path", photo->path);
        fputs(", \"metadata\": {", stdout);
        json_field("camera", photo->camera_model);
        fputs(", ", stdout);
        json_field("make", photo->camera_make);
        fputs(", \"iso\": ", stdout);
        if (photo->has_iso)
            printf("%" PRIu32, photo->iso);
        else
            fputs("null", stdout);
        fputs(", \"aperture\": ", stdout);
        json_number(photo->has_aperture, photo->aperture);
        fputs(", \"focal_length_mm\": ", stdout);
        json_number(photo->has_focal_length, photo->focal_length);
        fputs(", ", stdout);
        json_field("capture_timestamp", photo->capture_timestamp);
        printf(", \"has_gps\": %s, \"latitude\": ",
               photo->has_gps ? "true" : "false");
        json_number(photo->has_gps, photo->latitude);
        fputs(", \"longitude\": ", stdout);
        json_number(photo->has_gps, photo->longitude);
        printf(", \"width\": %" PRIu32 ", \"height\": %" PRIu32 "}}",
               photo->width, photo->height);
    }
    fputs(scan->count == 0 ? "],\n" : "\n  ],\n", stdout);
    printf("  \"summary\": {\"files_visited\": %" PRIu64
           ", \"jpeg_files_found\": %" PRIu64 ", \"photos_parsed\": %" PRIu64
           ", \"skipped_files\": %" PRIu64 ", \"errors\": %" PRIu64
           ", \"matched\": %zu}\n}\n",
           stats->files_visited, stats->jpeg_files_found, stats->photos_parsed,
           stats->skipped_files, stats->errors, scan->count);
}

int photoc_command_query_with_output(const char *path,
                                     const photoc_query *query, bool recursive,
                                     bool json, bool print0,
                                     const photoc_output *output)
{
    if (query == NULL || (json && print0))
        return photoc_error_report("query", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_USAGE, NULL,
                                   "query required; --json and --print0 are "
                                   "mutually exclusive",
                                   0);
    photoc_fs_type type;
    if (photoc_fs_get_type(path, &type) != 0)
        return photoc_error_report("query", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                   path, "unable to inspect path", errno);
    if (type == PHOTOC_FS_OTHER ||
        (type == PHOTOC_FS_FILE && !photoc_fs_is_jpeg(path)))
        return photoc_error_report(
            "query", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_UNSUPPORTED, path,
            "expected a regular JPEG file or directory", 0);
    if (type == PHOTOC_FS_FILE && recursive)
        return photoc_error_report("query", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_USAGE, NULL,
                                   "--recursive requires a directory", 0);
    photoc_output_verbose(output, "query",
                          "input: %s; mode: metadata search; recursive: %s; "
                          "output: %s; scanner workers: up to 2\n",
                          path, recursive ? "yes" : "no",
                          json     ? "JSON"
                          : print0 ? "NUL paths"
                                   : "line paths");
    query_scan scan = {.query = query};
    photoc_scan_stats stats = {0};
    int result;
    if (type == PHOTOC_FS_FILE) {
        Photo photo = {0};
        photoc_metadata_result loaded = photo_load_metadata(path, &photo);
        if (loaded != PHOTOC_METADATA_OK)
            return photoc_error_metadata("query", PHOTOC_ERR_NOTE_NONE, path,
                                         loaded, errno);
        stats = (photoc_scan_stats){
            .files_visited = 1, .jpeg_files_found = 1, .photos_parsed = 1};
        result = collect_match(&photo, &scan) ? 0 : 1;
        photo_cleanup(&photo);
    } else {
        result = photoc_scan_directory_filtered(
            path, recursive, PHOTOC_FORMATS_JPEG, collect_match, scan_failure,
            &scan, &stats);
    }
    if (result != 0) {
        int saved_errno = scan.error == 0 ? errno : scan.error;
        cleanup_scan(&scan);
        return photoc_error_report(
            "query", PHOTOC_ERR_NOTE_NONE,
            scan.error == 0 ? PHOTOC_ERR_IO : PHOTOC_ERR_INTERNAL, path,
            scan.error == 0 ? "unable to complete scan"
                            : "unable to retain matches",
            saved_errno);
    }
    if (scan.count > 1)
        qsort(scan.matches, scan.count, sizeof(*scan.matches), compare_photos);
    photoc_output_verbose(
        output, "query",
        "files scanned: %" PRIu64 "; JPEGs discovered: %" PRIu64
        "; parsed: %" PRIu64 "; skipped: %" PRIu64
        "; metadata failures: %" PRIu64 "; matched: %zu\n",
        stats.files_visited, stats.jpeg_files_found, stats.photos_parsed,
        stats.skipped_files, stats.errors, scan.count);
    if (json) {
        print_json(path, recursive, query, &scan, &stats);
    } else {
        for (size_t i = 0; i < scan.count; ++i) {
            fputs(scan.matches[i].path, stdout);
            fputc(print0 ? '\0' : '\n', stdout);
        }
    }
    cleanup_scan(&scan);
    if (ferror(stdout))
        return photoc_error_report("query", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                   NULL, "unable to write output", EIO);
    return stats.errors == 0 ? PHOTOC_EXIT_SUCCESS : PHOTOC_EXIT_FAILURE;
}

int photoc_command_query(const char *path, const photoc_query *query,
                         bool recursive, bool json, bool print0)
{
    return photoc_command_query_with_output(path, query, recursive, json,
                                            print0, NULL);
}
