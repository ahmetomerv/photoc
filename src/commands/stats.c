#include "photoc/commands.h"

#include "photoc/error.h"
#include "photoc/exit_codes.h"
#include "photoc/format.h"
#include "photoc/json.h"
#include "photoc/scan.h"
#include "photoc/stats.h"
#include "photoc/thread_pool.h"
#include "photoc/session.h"
#include "photoc/size.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    photoc_stats_aggregate aggregate;
    int aggregation_errno;
    const photoc_output *output; /* Borrowed during the scan. */
    photoc_progress *progress;   /* Borrowed; caller thread only. */
} stats_context;

static bool collect_photo(const Photo *photo, void *user_data)
{
    stats_context *context = user_data;
    if (photoc_stats_add_photo(&context->aggregate, photo) != 0) {
        context->aggregation_errno = errno;
        return false;
    }
    photoc_progress_increment(context->progress);
    return true;
}

static void report_warning(const char *path, photoc_metadata_result reason,
                           int system_errno, void *user_data)
{
    stats_context *context = user_data;
    photoc_progress_increment(context->progress);
    photoc_output_metadata_warning(context->output, "stats", path, reason,
                                   system_errno);
}

static void print_counts(const char *heading, const photoc_stats_counts *counts,
                         const char *prefix, const char *suffix,
                         uint64_t total_photos)
{
    printf("\n%s\n", heading);
    if (counts->count == 0) {
        puts("  None");
        return;
    }
    for (size_t i = 0; i < counts->count; ++i) {
        double percent =
            photoc_stats_percentage(counts->items[i].count, total_photos);
        printf("  %s%s%s: %" PRIu64 " (%.1f%%)\n", prefix,
               counts->items[i].value, suffix, counts->items[i].count, percent);
    }
}

static void print_top(const char *heading, const photoc_stats_counts *counts,
                      const char *prefix, const char *suffix)
{
    if (counts->count == 0) {
        printf("  %s: Unavailable\n", heading);
    } else {
        printf("  %s: %s%s%s (%" PRIu64 ")\n", heading, prefix,
               counts->items[0].value, suffix, counts->items[0].count);
    }
}

static void print_json_counts(const photoc_stats_counts *counts,
                              uint64_t total_photos, bool numeric_values)
{
    fputc('[', stdout);
    for (size_t i = 0; i < counts->count; ++i) {
        const photoc_stats_count *item = &counts->items[i];
        if (i != 0) {
            fputs(", ", stdout);
        }
        fputs("{\"value\": ", stdout);
        if (numeric_values) {
            fputs(item->value, stdout);
        } else {
            photoc_json_write_string(stdout, item->value);
        }
        printf(", \"count\": %" PRIu64 ", \"percentage_of_photos\": %.15g}",
               item->count, photoc_stats_percentage(item->count, total_photos));
    }
    fputc(']', stdout);
}

typedef struct {
    const char *key;
    const char *heading;
    const photoc_stats_counts *counts; /* Borrowed from aggregate. */
    const char *suffix;
    bool numeric;
    bool resolution;
} stats_view;

static void extended_views(const photoc_stats_aggregate *aggregate,
                           stats_view views[9])
{
    views[0] =
        (stats_view){"lens_models", "Lens models", &aggregate->lens_models, "",
                     false,         false};
    views[1] = (stats_view){"focal_lengths_35mm_equivalent_mm",
                            "Focal lengths (35mm equivalent)",
                            &aggregate->focal_lengths_35mm,
                            " mm",
                            true,
                            false};
    views[2] = (stats_view){"shutter_speeds_seconds",
                            "Shutter speeds",
                            &aggregate->shutter_speeds,
                            " s",
                            true,
                            false};
    views[3] = (stats_view){"orientations",
                            "Image orientation",
                            &aggregate->orientations,
                            "",
                            false,
                            false};
    views[4] = (stats_view){"resolutions",
                            "Resolutions / megapixels",
                            &aggregate->resolutions,
                            "",
                            false,
                            true};
    views[5] = (stats_view){
        "years", "Photos per calendar year", &aggregate->years, "", false,
        false};
    views[6] = (stats_view){
        "months", "Photos per calendar month", &aggregate->months, "", false,
        false};
    views[7] = (stats_view){
        "days", "Photos per calendar day", &aggregate->days, "", false, false};
    views[8] = (stats_view){
        "hours", "Photos by local hour", &aggregate->hours, "", true, false};
}

static void print_resolution_json(const photoc_stats_aggregate *aggregate)
{
    fputc('[', stdout);
    for (size_t i = 0; i < aggregate->resolutions.count; ++i) {
        const photoc_stats_count *item = &aggregate->resolutions.items[i];
        if (i != 0)
            fputs(", ", stdout);
        fputs("{\"value\": ", stdout);
        photoc_json_write_string(stdout, item->value);
        printf(", \"width\": %" PRIu32 ", \"height\": %" PRIu32
               ", \"megapixels\": %.15g, \"count\": %" PRIu64
               ", \"percentage_of_photos\": %.15g}",
               item->width, item->height,
               (double)((uint64_t)item->width * item->height) / 1000000.0,
               item->count,
               photoc_stats_percentage(item->count, aggregate->total_photos));
    }
    fputc(']', stdout);
}

static void print_extended_json(const photoc_stats_aggregate *aggregate)
{
    stats_view views[9];
    extended_views(aggregate, views);
    for (size_t i = 0; i < 9; ++i) {
        printf(",\n    \"%s\": ", views[i].key);
        if (views[i].resolution)
            print_resolution_json(aggregate);
        else
            print_json_counts(views[i].counts, aggregate->total_photos,
                              views[i].numeric);
    }
    fputs("\n  },\n  \"unavailable\": {", stdout);
    const photoc_stats_counts *old_counts[] = {
        &aggregate->camera_models, &aggregate->iso_values,
        &aggregate->apertures, &aggregate->focal_lengths};
    const char *old_keys[] = {"camera_models", "iso", "apertures",
                              "focal_lengths_mm"};
    for (size_t i = 0; i < 4; ++i)
        printf("%s\n    \"%s\": %" PRIu64, i == 0 ? "" : ",", old_keys[i],
               aggregate->total_photos - photoc_stats_available(old_counts[i]));
    for (size_t i = 0; i < 9; ++i)
        printf(",\n    \"%s\": %" PRIu64, views[i].key,
               aggregate->total_photos -
                   photoc_stats_available(views[i].counts));
    printf(",\n    \"file_size\": %" PRIu64 "\n  },\n  \"sessions\": {\n"
           "    \"gap_minutes\": %u,\n    \"dated_photos\": %zu,\n"
           "    \"unavailable_photos\": %" PRIu64 ",\n    \"count\": %" PRIu64
           ",\n    \"average_photos_per_session\": ",
           aggregate->total_photos - aggregate->photos_with_file_size,
           PHOTOC_SESSION_DEFAULT_GAP_MINUTES, aggregate->capture_seconds.count,
           aggregate->total_photos - (uint64_t)aggregate->capture_seconds.count,
           aggregate->session_count);
    if (aggregate->session_count != 0)
        printf("%.15g", (double)aggregate->capture_seconds.count /
                            (double)aggregate->session_count);
    else
        fputs("null", stdout);
    fputs(",\n    \"smallest_session\": ", stdout);
    if (aggregate->session_count != 0)
        printf("%" PRIu64, aggregate->smallest_session);
    else
        fputs("null", stdout);
    fputs(",\n    \"largest_session\": ", stdout);
    if (aggregate->session_count != 0)
        printf("%" PRIu64, aggregate->largest_session);
    else
        fputs("null", stdout);
    fputs("\n  }\n}\n", stdout);
}

static void print_json(const char *directory, bool recursive,
                       const photoc_scan_stats *scan,
                       const photoc_stats_aggregate *aggregate)
{
    fputs("{\n  \"scan\": {\n    \"directory\": ", stdout);
    photoc_json_write_string(stdout, directory);
    printf(",\n    \"recursive\": %s,\n"
           "    \"files_visited\": %" PRIu64 ",\n"
           "    \"jpeg_files_found\": %" PRIu64 ",\n"
           "    \"arw_files_found\": %" PRIu64 ",\n"
           "    \"metadata_files_found\": %" PRIu64 ",\n"
           "    \"photos_parsed\": %" PRIu64 ",\n"
           "    \"skipped_files\": %" PRIu64 ",\n"
           "    \"errors\": %" PRIu64 "\n  },\n",
           recursive ? "true" : "false", scan->files_visited,
           scan->jpeg_files_found, scan->arw_files_found,
           scan->metadata_files_found, scan->photos_parsed, scan->skipped_files,
           scan->errors);

    printf("  \"storage\": {\n"
           "    \"total_bytes\": %" PRIu64 ",\n"
           "    \"photos_with_file_size\": %" PRIu64 ",\n"
           "    \"average_file_size_bytes\": ",
           aggregate->total_file_size, aggregate->photos_with_file_size);
    double average;
    if (photoc_stats_average_file_size(aggregate, &average)) {
        printf("%.15g", average);
    } else {
        fputs("null", stdout);
    }
    fputs(",\n    \"median_file_size_bytes\": ", stdout);
    double median;
    if (photoc_stats_median_file_size(aggregate, &median))
        printf("%.15g", median);
    else
        fputs("null", stdout);
    fputs("\n  },\n  \"capture_dates\": {\n    \"earliest\": ", stdout);
    photoc_json_write_string(stdout, aggregate->has_capture_dates
                                         ? aggregate->earliest_capture
                                         : NULL);
    fputs(",\n    \"latest\": ", stdout);
    photoc_json_write_string(stdout, aggregate->has_capture_dates
                                         ? aggregate->latest_capture
                                         : NULL);
    fputs("\n  },\n  \"distributions\": {\n    \"camera_models\": ", stdout);
    print_json_counts(&aggregate->camera_models, aggregate->total_photos,
                      false);
    fputs(",\n    \"iso\": ", stdout);
    print_json_counts(&aggregate->iso_values, aggregate->total_photos, true);
    fputs(",\n    \"apertures\": ", stdout);
    print_json_counts(&aggregate->apertures, aggregate->total_photos, true);
    fputs(",\n    \"focal_lengths_mm\": ", stdout);
    print_json_counts(&aggregate->focal_lengths, aggregate->total_photos, true);
    print_extended_json(aggregate);
}

static void print_human(const char *directory, bool recursive,
                        const photoc_scan_stats *scan,
                        const photoc_stats_aggregate *aggregate,
                        const photoc_output *output)
{
    puts("Photo statistics");
    photoc_output_info(output, "  Directory: %s\n", directory);
    photoc_output_info(output, "  Scan: %s\n",
                       recursive ? "recursive" : "flat");
    printf("  Total photos: %" PRIu64 "\n", aggregate->total_photos);
    char size[PHOTOC_SIZE_TEXT_CAPACITY];
    photoc_size_format((double)aggregate->total_file_size, size);
    printf("  Total storage used: %s\n", size);
    double average;
    if (photoc_stats_average_file_size(aggregate, &average)) {
        photoc_size_format(average, size);
        printf("  Average file size: %s\n", size);
    } else {
        puts("  Average file size: Unavailable");
    }
    printf("  Earliest capture: %s\n", aggregate->has_capture_dates
                                           ? aggregate->earliest_capture
                                           : "Unavailable");
    printf("  Latest capture: %s\n", aggregate->has_capture_dates
                                         ? aggregate->latest_capture
                                         : "Unavailable");
    print_top("Most-used camera", &aggregate->camera_models, "", "");
    print_top("Most-used ISO", &aggregate->iso_values, "", "");
    print_top("Most-used aperture", &aggregate->apertures, "f/", "");
    print_top("Most-used focal length", &aggregate->focal_lengths, "", " mm");
    if (scan->arw_files_found == 0) {
        photoc_output_info(output,
                           "  Files: %" PRIu64 " visited, %" PRIu64
                           " JPEG, %" PRIu64 " skipped, %" PRIu64 " %s\n",
                           scan->files_visited, scan->jpeg_files_found,
                           scan->skipped_files, scan->errors,
                           scan->errors == 1 ? "error" : "errors");
    } else {
        photoc_output_info(
            output,
            "  Files: %" PRIu64 " visited, %" PRIu64 " JPEG, %" PRIu64
            " ARW, %" PRIu64 " skipped, %" PRIu64 " %s\n",
            scan->files_visited, scan->jpeg_files_found, scan->arw_files_found,
            scan->skipped_files, scan->errors,
            scan->errors == 1 ? "error" : "errors");
    }

    puts("\nDistributions (share of parsed photos)");
    print_counts("Camera models", &aggregate->camera_models, "", "",
                 aggregate->total_photos);
    print_counts("ISO", &aggregate->iso_values, "", "",
                 aggregate->total_photos);
    print_counts("Apertures", &aggregate->apertures, "f/", "",
                 aggregate->total_photos);
    print_counts("Focal lengths", &aggregate->focal_lengths, "", " mm",
                 aggregate->total_photos);
    puts("\nAdditional statistics");
    double median;
    if (photoc_stats_median_file_size(aggregate, &median)) {
        photoc_size_format(median, size);
        printf("  Median file size: %s\n", size);
    } else
        puts("  Median file size: Unavailable");
    printf("  Shooting sessions (gap > %u minutes): %" PRIu64 "\n",
           PHOTOC_SESSION_DEFAULT_GAP_MINUTES, aggregate->session_count);
    printf("  Dated photos: %zu; unavailable capture times: %" PRIu64 "\n",
           aggregate->capture_seconds.count,
           aggregate->total_photos -
               (uint64_t)aggregate->capture_seconds.count);
    if (aggregate->session_count != 0) {
        printf("  Average photos per session: %.2f\n",
               (double)aggregate->capture_seconds.count /
                   (double)aggregate->session_count);
        printf("  Photos per session: min %" PRIu64 ", max %" PRIu64 "\n",
               aggregate->smallest_session, aggregate->largest_session);
    } else
        puts("  Average photos per session: Unavailable");
    stats_view views[9];
    extended_views(aggregate, views);
    for (size_t i = 0; i < 9; ++i) {
        const stats_view *view = &views[i];
        if (view->resolution) {
            printf("\n%s\n", view->heading);
            if (view->counts->count == 0)
                puts("  None");
            for (size_t j = 0; j < view->counts->count; ++j) {
                const photoc_stats_count *item = &view->counts->items[j];
                printf("  %s (%.15g MP): %" PRIu64 " (%.1f%%)\n", item->value,
                       (double)((uint64_t)item->width * item->height) /
                           1000000.0,
                       item->count,
                       photoc_stats_percentage(item->count,
                                               aggregate->total_photos));
            }
        } else
            print_counts(view->heading, view->counts, "", view->suffix,
                         aggregate->total_photos);
        printf("  Unavailable: %" PRIu64 "\n",
               aggregate->total_photos - photoc_stats_available(view->counts));
    }
}

int photoc_command_stats_with_output(const char *directory, bool recursive,
                                     bool json, const photoc_output *output)
{
    photoc_output_verbose(
        output, "stats",
        "input: %s; mode: metadata statistics; recursive: %s; output: %s\n",
        directory, recursive ? "yes" : "no", json ? "JSON" : "human");
    photoc_output_verbose(
        output, "stats",
        "worker limit: %u (small batches and startup fallback run serially)\n",
        PHOTOC_DEFAULT_WORKERS);
    stats_context context = {.output = output};
    context.progress = output == NULL ? NULL : output->progress;
    photoc_stats_init(&context.aggregate);
    size_t total = 0;
    photoc_progress_set_message(context.progress, "Discovering photos...");
    if (photoc_progress_discover(context.progress, directory, recursive,
                                 PHOTOC_FORMATS_METADATA, &total) != 0) {
        photoc_progress_fail(context.progress, "Failed to scan directory");
        photoc_stats_cleanup(&context.aggregate);
        return photoc_error_report("stats", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                   directory, "unable to read directory",
                                   errno);
    }
    photoc_progress_set_message(context.progress, "Reading metadata...");
    if (context.progress != NULL && context.progress->enabled)
        photoc_progress_set_total(context.progress, total);
    photoc_scan_stats scan = {0};
    int result = photoc_scan_directory(directory, recursive, collect_photo,
                                       report_warning, &context, &scan);
    if (result != 0)
        photoc_progress_fail(context.progress, "Failed to scan directory");
    else {
        char message[96];
        if (scan.errors != 0) {
            snprintf(message, sizeof(message),
                     "Analyzed %" PRIu64 " photos; %" PRIu64
                     " could not be processed",
                     scan.photos_parsed, scan.errors);
            photoc_progress_warn(context.progress, message);
        } else {
            snprintf(message, sizeof(message), "Analyzed %" PRIu64 " photos",
                     scan.photos_parsed);
            photoc_progress_finish(context.progress, message);
        }
    }
    photoc_output_verbose(output, "stats",
                          "ARW files discovered: %" PRIu64 "\n",
                          scan.arw_files_found);
    photoc_output_verbose(output, "stats",
                          "files scanned: %" PRIu64
                          "; JPEG files discovered: %" PRIu64
                          "; metadata parsed: %" PRIu64 "; skipped: %" PRIu64
                          "; metadata parse failures: %" PRIu64 "\n",
                          scan.files_visited, scan.jpeg_files_found,
                          scan.photos_parsed, scan.skipped_files, scan.errors);
    if (result != 0) {
        int saved_errno = result == 1 ? context.aggregation_errno : errno;
        photoc_error_kind kind =
            result == 1 ? PHOTOC_ERR_INTERNAL : PHOTOC_ERR_IO;
        const char *detail = result == 1 ? "unable to summarize photos"
                                         : "unable to read directory";
        photoc_error_report("stats", PHOTOC_ERR_NOTE_NONE, kind, directory,
                            detail, saved_errno);
        photoc_stats_cleanup(&context.aggregate);
        return PHOTOC_EXIT_FAILURE;
    }

    photoc_stats_sort(&context.aggregate);
    if (json) {
        print_json(directory, recursive, &scan, &context.aggregate);
    } else {
        print_human(directory, recursive, &scan, &context.aggregate, output);
    }

    photoc_stats_cleanup(&context.aggregate);
    if (ferror(stdout)) {
        return photoc_error_report("stats", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                   NULL, "unable to write output", EIO);
    }
    return PHOTOC_EXIT_SUCCESS;
}

int photoc_command_stats(const char *directory, bool recursive, bool json)
{
    return photoc_command_stats_with_output(directory, recursive, json, NULL);
}
