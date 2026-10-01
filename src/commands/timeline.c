#include "photoc/commands.h"
#include "photoc/error.h"
#include "photoc/exit_codes.h"
#include "photoc/format.h"
#include "photoc/json.h"
#include "photoc/scan.h"
#include "photoc/size.h"
#include "photoc/thread_pool.h"
#include "photoc/timeline.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    photoc_timeline timeline;
    int error;
    const photoc_output *output; /* Borrowed for synchronous callbacks. */
    photoc_progress *progress;   /* Borrowed; caller thread only. */
} timeline_context;

static bool collect_photo(const Photo *photo, void *data)
{
    timeline_context *context = data;
    if (photoc_timeline_add(&context->timeline, photo) != 0) {
        context->error = errno;
        return false;
    }
    photoc_progress_increment(context->progress);
    return true;
}

static void metadata_warning(const char *path, photoc_metadata_result reason,
                             int system_errno, void *data)
{
    timeline_context *context = data;
    photoc_progress_increment(context->progress);
    photoc_output_metadata_warning(context->output, "timeline", path, reason,
                                   system_errno);
}

static void write_top_json(const photoc_stats_counts *counts)
{
    if (counts->count != 0)
        printf("%.15g", counts->items[0].numeric_value);
    else
        fputs("null", stdout);
}

static void write_session_json(const photoc_timeline_session *session)
{
    const photoc_stats_aggregate *stats = &session->statistics;
    fputs("{\"start\": ", stdout);
    photoc_json_write_string(stdout, session->start + 11);
    fputs(", \"end\": ", stdout);
    photoc_json_write_string(stdout, session->end + 11);
    printf(", \"duration_seconds\": %" PRIu64 ", \"photo_count\": %" PRIu64
           ", \"total_file_size_bytes\": %" PRIu64
           ", \"photos_with_file_size\": %" PRIu64
           ", \"most_used_focal_length_mm\": ",
           session->duration_seconds, stats->total_photos,
           stats->total_file_size, stats->photos_with_file_size);
    write_top_json(&stats->focal_lengths);
    fputs(", \"most_used_aperture\": ", stdout);
    write_top_json(&stats->apertures);
    fputs(", \"iso_min\": ", stdout);
    if (session->has_iso)
        printf("%" PRIu32, session->iso_min);
    else
        fputs("null", stdout);
    fputs(", \"iso_max\": ", stdout);
    if (session->has_iso)
        printf("%" PRIu32, session->iso_max);
    else
        fputs("null", stdout);
    fputs(", \"camera_models\": [", stdout);
    for (size_t i = 0; i < stats->camera_models.count; ++i) {
        const photoc_stats_count *camera = &stats->camera_models.items[i];
        if (i != 0)
            fputs(", ", stdout);
        fputs("{\"model\": ", stdout);
        photoc_json_write_string(stdout, camera->value);
        printf(", \"photo_count\": %" PRIu64 "}", camera->count);
    }
    fputs("]}", stdout);
}

static void write_json(const char *directory, bool recursive, uint32_t gap,
                       const photoc_timeline *timeline,
                       const photoc_scan_stats *scan)
{
    fputs("{\n  \"directory\": ", stdout);
    photoc_json_write_string(stdout, directory);
    printf(
        ",\n  \"recursive\": %s,\n  \"gap_minutes\": %" PRIu32
        ",\n  \"time_basis\": \"recorded_local_exif\",\n  \"summary\": {\n"
        "    \"files_visited\": %" PRIu64 ",\n    \"photos_parsed\": %" PRIu64
        ",\n    \"photos_included\": %zu,\n    \"photos_skipped_timestamp\": "
        "%" PRIu64 ",\n    \"missing_timestamp\": %" PRIu64
        ",\n    \"invalid_timestamp\": %" PRIu64
        ",\n    \"metadata_errors\": %" PRIu64
        ",\n    \"unsupported_files\": %" PRIu64
        ",\n    \"dates\": %zu,\n    \"sessions\": %zu\n  },\n  \"dates\": [",
        recursive ? "true" : "false", gap, scan->files_visited,
        scan->photos_parsed, timeline->photo_count,
        timeline->skipped_missing_timestamp +
            timeline->skipped_invalid_timestamp,
        timeline->skipped_missing_timestamp,
        timeline->skipped_invalid_timestamp, scan->errors,
        scan->skipped_files - scan->errors, timeline->date_count,
        timeline->session_count);
    for (size_t i = 0; i < timeline->session_count; ++i) {
        const photoc_timeline_session *session = &timeline->sessions[i];
        bool new_date = i == 0 || strcmp(session->date,
                                         timeline->sessions[i - 1].date) != 0;
        if (new_date) {
            if (i != 0)
                fputs("]},", stdout);
            fputs("\n    {\"date\": ", stdout);
            photoc_json_write_string(stdout, session->date);
            fputs(", \"sessions\": [", stdout);
        } else
            fputs(", ", stdout);
        write_session_json(session);
    }
    if (timeline->session_count != 0)
        fputs("]}\n  ", stdout);
    fputs("]\n}\n", stdout);
}

static void write_human(const char *directory, uint32_t gap,
                        const photoc_timeline *timeline,
                        const photoc_scan_stats *scan,
                        const photoc_output *output)
{
    photoc_output_info(
        output,
        "Shooting timeline: %s\nSession gap: %" PRIu32
        " minutes; recorded EXIF times (timezone unspecified)\n\n",
        directory, gap);
    for (size_t i = 0; i < timeline->session_count; ++i) {
        const photoc_timeline_session *session = &timeline->sessions[i];
        const photoc_stats_aggregate *stats = &session->statistics;
        if (i == 0 ||
            strcmp(session->date, timeline->sessions[i - 1].date) != 0)
            printf("%s\n\n", session->date);
        char size[PHOTOC_SIZE_TEXT_CAPACITY];
        photoc_size_format((double)stats->total_file_size, size);
        printf("%s - %s\n  %" PRIu64 " photos\n  Duration: %" PRIu64
               " seconds\n  Total file size: %s\n",
               session->start + 11, session->end + 11, stats->total_photos,
               session->duration_seconds, size);
        if (stats->focal_lengths.count != 0)
            printf("  Most-used focal length: %s mm\n",
                   stats->focal_lengths.items[0].value);
        if (stats->apertures.count != 0)
            printf("  Most-used aperture: f/%s\n",
                   stats->apertures.items[0].value);
        if (session->has_iso)
            printf("  ISO range: %" PRIu32 " - %" PRIu32 "\n", session->iso_min,
                   session->iso_max);
        if (stats->camera_models.count != 0) {
            puts("  Camera models:");
            for (size_t j = 0; j < stats->camera_models.count; ++j)
                printf("    %s: %" PRIu64 "\n",
                       stats->camera_models.items[j].value,
                       stats->camera_models.items[j].count);
        }
        putchar('\n');
    }
    printf("Photos included: %zu\nDates: %zu\nSessions: %zu\n"
           "Skipped capture timestamps: %" PRIu64 " (%" PRIu64
           " missing, %" PRIu64 " invalid)\n"
           "Metadata errors: %" PRIu64 "\nUnsupported files: %" PRIu64 "\n",
           timeline->photo_count, timeline->date_count, timeline->session_count,
           timeline->skipped_missing_timestamp +
               timeline->skipped_invalid_timestamp,
           timeline->skipped_missing_timestamp,
           timeline->skipped_invalid_timestamp, scan->errors,
           scan->skipped_files - scan->errors);
}

int photoc_command_timeline_with_output(const char *directory, bool recursive,
                                        uint32_t gap_minutes, bool json,
                                        const photoc_output *output)
{
    photoc_output_verbose(output, "timeline",
                          "input: %s; mode: recorded EXIF timeline; "
                          "recursive: %s; gap: %" PRIu32
                          " minutes; output: %s; worker limit: %u\n",
                          directory, recursive ? "yes" : "no", gap_minutes,
                          json ? "JSON" : "human", PHOTOC_DEFAULT_WORKERS);
    photoc_progress *progress = output == NULL ? NULL : output->progress;
    timeline_context context = {.output = output, .progress = progress};
    photoc_timeline_init(&context.timeline);
    size_t total = 0;
    photoc_progress_set_message(progress, "Discovering photos...");
    if (photoc_progress_discover(progress, directory, recursive,
                                 PHOTOC_FORMATS_METADATA, &total) != 0) {
        photoc_progress_fail(progress, "Failed to scan directory");
        photoc_timeline_cleanup(&context.timeline);
        return photoc_error_report("timeline", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_IO, directory,
                                   "unable to read directory", errno);
    }
    photoc_progress_set_message(progress, "Reading metadata...");
    if (progress != NULL && progress->enabled)
        photoc_progress_set_total(progress, total);
    photoc_scan_stats scan = {0};
    int result = photoc_scan_directory(directory, recursive, collect_photo,
                                       metadata_warning, &context, &scan);
    if (result != 0) {
        photoc_progress_fail(progress, "Failed to scan directory");
        int saved_errno = result == 1 ? context.error : errno;
        photoc_timeline_cleanup(&context.timeline);
        return photoc_error_report(
            "timeline", PHOTOC_ERR_NOTE_NONE,
            result == 1 ? PHOTOC_ERR_INTERNAL : PHOTOC_ERR_IO, directory,
            result == 1 ? "unable to retain timeline metadata"
                        : "unable to read directory",
            saved_errno);
    }
    if (photoc_timeline_finish(&context.timeline, gap_minutes) != 0) {
        photoc_progress_fail(progress, "Failed to build timeline");
        int saved_errno = errno;
        photoc_timeline_cleanup(&context.timeline);
        return photoc_error_report("timeline", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_INTERNAL, directory,
                                   "unable to build timeline", saved_errno);
    }
    char message[96];
    if (scan.errors != 0) {
        snprintf(message, sizeof(message),
                 "Analyzed %" PRIu64 " photos; %" PRIu64
                 " could not be processed",
                 scan.photos_parsed, scan.errors);
        photoc_progress_warn(progress, message);
    } else {
        snprintf(message, sizeof(message), "Analyzed %" PRIu64 " photos",
                 scan.photos_parsed);
        photoc_progress_finish(progress, message);
    }
    photoc_output_verbose(
        output, "timeline",
        "files scanned: %" PRIu64 "; metadata parsed: %" PRIu64
        "; JPEG found: %" PRIu64 "; ARW found: %" PRIu64
        "; metadata failures: %" PRIu64 "; missing timestamps: %" PRIu64
        "; invalid timestamps: %" PRIu64 "; sessions: %zu\n",
        scan.files_visited, scan.photos_parsed, scan.jpeg_files_found,
        scan.arw_files_found, scan.errors,
        context.timeline.skipped_missing_timestamp,
        context.timeline.skipped_invalid_timestamp,
        context.timeline.session_count);
    if (json)
        write_json(directory, recursive, gap_minutes, &context.timeline, &scan);
    else
        write_human(directory, gap_minutes, &context.timeline, &scan, output);
    photoc_timeline_cleanup(&context.timeline);
    if (ferror(stdout))
        return photoc_error_report("timeline", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_IO, NULL,
                                   "unable to write output", EIO);
    return PHOTOC_EXIT_SUCCESS;
}

int photoc_command_timeline(const char *directory, bool recursive,
                            uint32_t gap_minutes, bool json)
{
    return photoc_command_timeline_with_output(directory, recursive,
                                               gap_minutes, json, NULL);
}
