#include "photoc/commands.h"

#include "photoc/error.h"
#include "photoc/exit_codes.h"
#include "photoc/json.h"
#include "photoc/scan.h"
#include "photoc/stats.h"
#include "photoc/thread_pool.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    photoc_stats_aggregate aggregate;
    int aggregation_errno;
    const photoc_output *output; /* Borrowed during the scan. */
} stats_context;

static bool collect_photo(const Photo *photo, void *user_data)
{
    stats_context *context = user_data;
    if (photoc_stats_add_photo(&context->aggregate, photo) != 0) {
        context->aggregation_errno = errno;
        return false;
    }
    return true;
}

static void report_warning(const char *path, photoc_metadata_result reason,
                           int system_errno, void *user_data)
{
    const stats_context *context = user_data;
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
            100.0 * (double)counts->items[i].count / (double)total_photos;
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
               item->count, 100.0 * (double)item->count / (double)total_photos);
    }
    fputc(']', stdout);
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
    fputs("\n  }\n}\n", stdout);
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
    printf("  Total storage used: %" PRIu64 " bytes\n",
           aggregate->total_file_size);
    double average;
    if (photoc_stats_average_file_size(aggregate, &average)) {
        printf("  Average file size: %.1f bytes\n", average);
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
    photoc_stats_init(&context.aggregate);
    photoc_scan_stats scan = {0};
    int result = photoc_scan_directory(directory, recursive, collect_photo,
                                       report_warning, &context, &scan);
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
