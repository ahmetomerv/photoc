#include "photoc/commands.h"

#include "photoc/duplicates.h"
#include "photoc/exit_codes.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void report_warning(const char *path, int system_errno, void *user_data)
{
    (void)user_data;
    fprintf(stderr, "photoc duplicates: warning: '%s': %s\n",
            path, strerror(system_errno));
}

int photoc_command_duplicates(const char *directory, bool recursive)
{
    photoc_duplicates_result result;
    if (photoc_duplicates_find(directory, recursive, report_warning,
                               NULL, &result) != 0) {
        fprintf(stderr, "photoc duplicates: '%s': %s\n",
                directory, strerror(errno));
        return PHOTOC_EXIT_FAILURE;
    }

    puts("Exact duplicate files");
    printf("  Directory: %s\n", directory);
    printf("  Scan: %s\n", recursive ? "recursive" : "flat");
    if (result.group_count == 0) {
        puts("\nNo duplicates found.");
    }
    for (size_t i = 0; i < result.group_count; ++i) {
        const photoc_duplicate_group *group = &result.groups[i];
        printf("\nGroup %zu (%" PRIu64 " bytes each, %zu files)\n",
               i + 1, group->file_size, group->count);
        for (size_t j = 0; j < group->count; ++j) {
            printf("  %s\n", group->paths[j]);
        }
    }
    printf("\nTotal duplicate groups: %zu\n", result.group_count);
    printf("Total duplicate files: %" PRIu64 "\n", result.duplicate_files);
    printf("Potential storage savings: %" PRIu64 " bytes\n",
           result.potential_savings);
    printf("Files scanned: %" PRIu64 ", hashed: %" PRIu64
           ", skipped: %" PRIu64 "\n",
           result.files_visited, result.files_hashed, result.files_skipped);

    int exit_code = result.errors == 0 ? PHOTOC_EXIT_SUCCESS : PHOTOC_EXIT_FAILURE;
    photoc_duplicates_cleanup(&result);
    if (ferror(stdout)) {
        fputs("photoc duplicates: unable to write output\n", stderr);
        return PHOTOC_EXIT_FAILURE;
    }
    return exit_code;
}
