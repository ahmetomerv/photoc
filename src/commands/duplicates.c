#include "photoc/commands.h"

#include "photoc/duplicates.h"
#include "photoc/error.h"
#include "photoc/exit_codes.h"
#include "photoc/json.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void report_warning(const char *path, int system_errno, void *user_data)
{
    (void)user_data;
    photoc_error_report("duplicates", PHOTOC_ERR_NOTE_WARNING, PHOTOC_ERR_IO,
                        path, "unable to read file", system_errno);
}

static void print_human(const char *directory, bool recursive,
                        const photoc_duplicates_result *result)
{
    puts("Exact duplicate files");
    printf("  Directory: %s\n", directory);
    printf("  Scan: %s\n", recursive ? "recursive" : "flat");
    if (result->group_count == 0) {
        puts("\nNo duplicates found.");
    }
    for (size_t i = 0; i < result->group_count; ++i) {
        const photoc_duplicate_group *group = &result->groups[i];
        printf("\nGroup %zu (%" PRIu64 " bytes each, %zu files)\n", i + 1,
               group->file_size, group->count);
        for (size_t j = 0; j < group->count; ++j) {
            printf("  %s\n", group->paths[j]);
        }
    }
    printf("\nTotal duplicate groups: %zu\n", result->group_count);
    printf("Total duplicate files: %" PRIu64 "\n", result->duplicate_files);
    printf("Potential storage savings: %" PRIu64 " bytes\n",
           result->potential_savings);
    printf("Files scanned: %" PRIu64 ", hashed: %" PRIu64 ", skipped: %" PRIu64
           "\n",
           result->files_visited, result->files_hashed, result->files_skipped);
}

static int print_json(const char *directory, bool recursive,
                      const photoc_duplicates_result *result)
{
    fputs("{\n  \"directory\": ", stdout);
    photoc_json_write_string(stdout, directory);
    printf(",\n  \"recursive\": %s,\n"
           "  \"duplicate_group_count\": %zu,\n"
           "  \"duplicate_file_count\": %" PRIu64 ",\n"
           "  \"potential_savings_bytes\": %" PRIu64 ",\n"
           "  \"groups\": [",
           recursive ? "true" : "false", result->group_count,
           result->duplicate_files, result->potential_savings);
    for (size_t i = 0; i < result->group_count; ++i) {
        const photoc_duplicate_group *group = &result->groups[i];
        char hash[PHOTOC_SHA256_HEX_SIZE];
        if (photoc_hash_sha256_hex(group->sha256, hash) != 0) {
            return -1;
        }
        printf("%s\n    {\"file_size_bytes\": %" PRIu64 ", \"sha256\": ",
               i == 0 ? "" : ",", group->file_size);
        photoc_json_write_string(stdout, hash);
        fputs(", \"paths\": [", stdout);
        for (size_t j = 0; j < group->count; ++j) {
            if (j != 0) {
                fputs(", ", stdout);
            }
            photoc_json_write_string(stdout, group->paths[j]);
        }
        fputs("]}", stdout);
    }
    fputs(result->group_count == 0 ? "]\n}\n" : "\n  ]\n}\n", stdout);
    return ferror(stdout) ? -1 : 0;
}

int photoc_command_duplicates(const char *directory, bool recursive, bool json)
{
    photoc_duplicates_result result;
    if (photoc_duplicates_find(directory, recursive, report_warning, NULL,
                               &result) != 0) {
        return photoc_error_report("duplicates", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_IO, directory,
                                   "unable to read directory", errno);
    }

    if (json) {
        if (print_json(directory, recursive, &result) != 0) {
            photoc_duplicates_cleanup(&result);
            return photoc_error_report("duplicates", PHOTOC_ERR_NOTE_NONE,
                                       PHOTOC_ERR_IO, NULL,
                                       "unable to write output", EIO);
        }
    } else {
        print_human(directory, recursive, &result);
    }

    int exit_code =
        result.errors == 0 ? PHOTOC_EXIT_SUCCESS : PHOTOC_EXIT_FAILURE;
    photoc_duplicates_cleanup(&result);
    if (ferror(stdout)) {
        return photoc_error_report("duplicates", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_IO, NULL,
                                   "unable to write output", EIO);
    }
    return exit_code;
}
