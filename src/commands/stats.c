#include "photoc/commands.h"

#include "photoc/exit_codes.h"
#include "photoc/scan.h"
#include "photoc/stats.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    photoc_stats_aggregate aggregate;
    int aggregation_errno;
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
    (void)user_data;
    fprintf(stderr, "photoc stats: warning: '%s': %s", path,
            photo_metadata_result_message(reason));
    if (reason == PHOTOC_METADATA_IO_ERROR) {
        fprintf(stderr, ": %s", strerror(system_errno));
    }
    fputc('\n', stderr);
}

static void print_counts(const char *heading, const photoc_stats_counts *counts,
                         const char *prefix, const char *suffix)
{
    printf("\n%s\n", heading);
    if (counts->count == 0) {
        puts("  None");
        return;
    }
    for (size_t i = 0; i < counts->count; ++i) {
        printf("  %s%s%s: %" PRIu64 "\n", prefix, counts->items[i].value,
               suffix, counts->items[i].count);
    }
}

int photoc_command_stats(const char *directory, bool recursive)
{
    stats_context context = {0};
    photoc_stats_init(&context.aggregate);
    photoc_scan_stats scan = {0};
    int result = photoc_scan_directory(directory, recursive, collect_photo,
                                       report_warning, &context, &scan);
    if (result != 0) {
        int saved_errno = result == 1 ? context.aggregation_errno : errno;
        fprintf(stderr, "photoc stats: '%s': %s\n", directory,
                strerror(saved_errno));
        photoc_stats_cleanup(&context.aggregate);
        return PHOTOC_EXIT_FAILURE;
    }

    photoc_stats_sort(&context.aggregate);
    puts("Photo statistics");
    printf("  Directory: %s\n", directory);
    printf("  Scan: %s\n", recursive ? "recursive" : "flat");
    printf("  Total photos: %" PRIu64 "\n", context.aggregate.total_photos);
    printf("  Total file size: %" PRIu64 " bytes\n",
           context.aggregate.total_file_size);
    printf("  Files visited: %" PRIu64 "\n", scan.files_visited);
    printf("  JPEG files found: %" PRIu64 "\n", scan.jpeg_files_found);
    printf("  Skipped files: %" PRIu64 "\n", scan.skipped_files);
    printf("  Errors: %" PRIu64 "\n", scan.errors);

    print_counts("Camera models", &context.aggregate.camera_models, "", "");
    print_counts("ISO", &context.aggregate.iso_values, "", "");
    print_counts("Apertures", &context.aggregate.apertures, "f/", "");
    print_counts("Focal lengths", &context.aggregate.focal_lengths, "", " mm");

    photoc_stats_cleanup(&context.aggregate);
    if (ferror(stdout)) {
        fputs("photoc stats: unable to write output\n", stderr);
        return PHOTOC_EXIT_FAILURE;
    }
    return PHOTOC_EXIT_SUCCESS;
}
