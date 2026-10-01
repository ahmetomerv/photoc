#include "photoc/commands.h"

#include "photoc/duplicates.h"
#include "photoc/error.h"
#include "photoc/exit_codes.h"
#include "photoc/json.h"
#include "photoc/size.h"
#include "photoc/thread_pool.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void report_warning(const char *path, int system_errno, void *user_data)
{
    /* These warnings explain exit status 1 and must remain visible in quiet. */
    (void)user_data;
    photoc_error_report("duplicates", PHOTOC_ERR_NOTE_WARNING, PHOTOC_ERR_IO,
                        path, "unable to read file", system_errno);
}

static void print_human(const char *directory, bool recursive,
                        const photoc_duplicates_result *result,
                        const photoc_output *output)
{
    puts("Exact duplicate files");
    photoc_output_info(output, "  Directory: %s\n", directory);
    photoc_output_info(output, "  Scan: %s\n",
                       recursive ? "recursive" : "flat");
    if (result->group_count == 0) {
        puts("\nNo duplicates found.");
    }
    for (size_t i = 0; i < result->group_count; ++i) {
        const photoc_duplicate_group *group = &result->groups[i];
        char size[PHOTOC_SIZE_TEXT_CAPACITY];
        photoc_size_format((double)group->file_size, size);
        printf("\nGroup %zu (%s each, %zu files)\n", i + 1, size,
               group->count);
        for (size_t j = 0; j < group->count; ++j) {
            printf("  %s\n", group->paths[j]);
        }
    }
    printf("\nTotal duplicate groups: %zu\n", result->group_count);
    printf("Total duplicate files: %" PRIu64 "\n", result->duplicate_files);
    char savings[PHOTOC_SIZE_TEXT_CAPACITY];
    photoc_size_format((double)result->potential_savings, savings);
    printf("Potential storage savings: %s\n", savings);
    photoc_output_info(output,
                       "Files scanned: %" PRIu64 ", hashed: %" PRIu64
                       ", skipped: %" PRIu64 "\n",
                       result->files_visited, result->files_hashed,
                       result->files_skipped);
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

int photoc_command_duplicates_with_output(const char *directory, bool recursive,
                                          bool json,
                                          const photoc_output *output)
{
    photoc_output_verbose(
        output, "duplicates",
        "input: %s; mode: exact duplicates; recursive: %s; output: %s\n",
        directory, recursive ? "yes" : "no", json ? "JSON" : "human");
    photoc_output_verbose(output, "duplicates",
                          "worker limit: %u (small workloads and startup "
                          "fallback run serially)\n",
                          PHOTOC_DEFAULT_WORKERS);
    photoc_duplicates_result result;
    photoc_progress *progress = output == NULL ? NULL : output->progress;
    if (photoc_duplicates_find_progress(directory, recursive, report_warning,
                                        NULL, &result, progress) != 0) {
        photoc_progress_fail(progress, "Failed to scan directory");
        return photoc_error_report("duplicates", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_IO, directory,
                                   "unable to read directory", errno);
    }
    char message[96];
    if (result.errors != 0) {
        snprintf(message, sizeof(message),
                 "Checked %" PRIu64 " files; %" PRIu64 " could not be read",
                 result.files_visited, result.errors);
        photoc_progress_warn(progress, message);
    } else {
        snprintf(message, sizeof(message), "Checked %" PRIu64 " files",
                 result.files_visited);
        photoc_progress_finish(progress, message);
    }

    photoc_output_verbose(output, "duplicates",
                          "files scanned: %" PRIu64 "; full hashes: %" PRIu64
                          "; skipped: %" PRIu64 "; file failures: %" PRIu64
                          "; duplicate groups: %zu\n",
                          result.files_visited, result.files_hashed,
                          result.files_skipped, result.errors,
                          result.group_count);
    if (json) {
        if (print_json(directory, recursive, &result) != 0) {
            photoc_duplicates_cleanup(&result);
            return photoc_error_report("duplicates", PHOTOC_ERR_NOTE_NONE,
                                       PHOTOC_ERR_IO, NULL,
                                       "unable to write output", EIO);
        }
    } else {
        print_human(directory, recursive, &result, output);
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

int photoc_command_duplicates(const char *directory, bool recursive, bool json)
{
    return photoc_command_duplicates_with_output(directory, recursive, json,
                                                 NULL);
}
