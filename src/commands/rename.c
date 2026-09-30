#define _POSIX_C_SOURCE 200809L

#include "photoc/commands.h"

#include "photoc/error.h"
#include "photoc/exit_codes.h"
#include "photoc/filename_template.h"
#include "photoc/fs.h"
#include "photoc/photo.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct {
    char *source;
    char *destination;
    photoc_metadata_result metadata_error;
    photoc_template_result template_error;
    size_t template_offset;
    int system_errno;
    int source_errno;
    dev_t source_device;
    ino_t source_inode;
    bool source_changed;
    bool destination_exists;
    bool duplicate_destination;
    bool applied;
} rename_entry;

typedef struct {
    rename_entry *entries;
    size_t count;
    size_t capacity;
    int error_errno;
    size_t skipped;
    size_t arw_count;
    const photoc_output *output; /* Borrowed for the synchronous walk. */
    photoc_progress *progress; /* Borrowed; caller thread only. */
} rename_plan;

typedef struct {
    size_t planned;
    size_t unchanged;
    size_t blocked;
    size_t applied;
    size_t rolled_back;
} rename_summary;

static void free_plan(rename_plan *plan)
{
    for (size_t i = 0; i < plan->count; ++i) {
        free(plan->entries[i].source);
        free(plan->entries[i].destination);
    }
    free(plan->entries);
    *plan = (rename_plan){0};
}

static bool collect_photo(const char *path, photoc_fs_type type,
                          void *user_data)
{
    rename_plan *plan = user_data;
    if (photoc_progress_interrupted()) {
        plan->error_errno = EINTR;
        return false;
    }
    if (type != PHOTOC_FS_FILE ||
        !photoc_format_is_selected(path, PHOTOC_FORMATS_METADATA)) {
        if (type != PHOTOC_FS_DIRECTORY) {
            ++plan->skipped;
            photoc_output_verbose(
                plan->output, "rename",
                "skipped '%s': not a supported metadata photo\n", path);
        }
        return true;
    }
    if (plan->count == plan->capacity) {
        size_t capacity = plan->capacity == 0 ? 16 : plan->capacity * 2;
        if (capacity < plan->capacity ||
            capacity > SIZE_MAX / sizeof(*plan->entries)) {
            plan->error_errno = EOVERFLOW;
            return false;
        }
        rename_entry *entries =
            realloc(plan->entries, capacity * sizeof(*entries));
        if (entries == NULL) {
            plan->error_errno = errno;
            return false;
        }
        plan->entries = entries;
        plan->capacity = capacity;
    }
    size_t length = strlen(path);
    if (length == SIZE_MAX) {
        plan->error_errno = EOVERFLOW;
        return false;
    }
    char *copy = malloc(length + 1);
    if (copy == NULL) {
        plan->error_errno = errno;
        return false;
    }
    memcpy(copy, path, length + 1);
    if (photoc_format_from_path(path) == PHOTOC_FORMAT_SONY_ARW)
        ++plan->arw_count;
    plan->entries[plan->count++] = (rename_entry){.source = copy};
    photoc_progress_update(plan->progress, plan->count);
    return true;
}

static int compare_source(const void *left, const void *right)
{
    const rename_entry *a = left;
    const rename_entry *b = right;
    return strcmp(a->source, b->source);
}

static int destination_path(const char *source, const char *name, char **output)
{
    const char *slash = strrchr(source, '/');
    size_t parent_length = slash == NULL     ? 1
                           : slash == source ? 1
                                             : (size_t)(slash - source);
    char *parent = malloc(parent_length + 1);
    if (parent == NULL) {
        return -1;
    }
    if (slash == NULL) {
        memcpy(parent, ".", 2);
    } else {
        memcpy(parent, source, parent_length);
        parent[parent_length] = '\0';
    }
    int result = photoc_fs_join(parent, name, output);
    free(parent);
    return result;
}

static int prepare_entries(rename_plan *plan, const char *format)
{
    for (size_t i = 0; i < plan->count; ++i) {
        if (photoc_progress_interrupted()) {
            errno = EINTR;
            return -1;
        }
        photoc_progress_update(plan->progress, i);
        rename_entry *entry = &plan->entries[i];
        struct stat source_info;
        if (lstat(entry->source, &source_info) != 0) {
            entry->source_errno = errno;
            continue;
        }
        if (!S_ISREG(source_info.st_mode)) {
            entry->source_changed = true;
            continue;
        }
        entry->source_device = source_info.st_dev;
        entry->source_inode = source_info.st_ino;

        Photo photo = {0};
        entry->metadata_error = photo_load_metadata(entry->source, &photo);
        if (entry->metadata_error != PHOTOC_METADATA_OK) {
            entry->system_errno = errno;
            if (entry->metadata_error == PHOTOC_METADATA_NO_MEMORY) {
                errno = ENOMEM;
                return -1;
            }
            continue;
        }

        char *name = NULL;
        entry->template_error = photoc_filename_template_expand(
            format, &photo, (uint64_t)i + 1, 4, &name, &entry->template_offset);
        photo_cleanup(&photo);
        if (entry->template_error != PHOTOC_TEMPLATE_OK) {
            if (entry->template_error == PHOTOC_TEMPLATE_NO_MEMORY ||
                entry->template_error == PHOTOC_TEMPLATE_TOO_LONG) {
                errno = entry->template_error == PHOTOC_TEMPLATE_NO_MEMORY
                            ? ENOMEM
                            : EOVERFLOW;
                return -1;
            }
            continue;
        }
        if (destination_path(entry->source, name, &entry->destination) != 0) {
            free(name);
            return -1;
        }
        free(name);

        if (lstat(entry->source, &source_info) != 0) {
            entry->source_errno = errno;
        } else if (!S_ISREG(source_info.st_mode) ||
                   source_info.st_dev != entry->source_device ||
                   source_info.st_ino != entry->source_inode) {
            entry->source_changed = true;
        }

        bool exists = false;
        if (photoc_fs_exists(entry->destination, &exists) != 0) {
            entry->system_errno = errno;
        } else if (exists && strcmp(entry->source, entry->destination) != 0) {
            entry->destination_exists = true;
        }
    }
    return 0;
}

static void recheck_entries(rename_plan *plan)
{
    for (size_t i = 0; i < plan->count; ++i) {
        rename_entry *entry = &plan->entries[i];
        if (entry->metadata_error != PHOTOC_METADATA_OK ||
            entry->template_error != PHOTOC_TEMPLATE_OK ||
            entry->destination == NULL) {
            continue;
        }
        struct stat source_info;
        if (lstat(entry->source, &source_info) != 0) {
            entry->source_errno = errno;
        } else if (!S_ISREG(source_info.st_mode) ||
                   source_info.st_dev != entry->source_device ||
                   source_info.st_ino != entry->source_inode) {
            entry->source_changed = true;
        }
        if (strcmp(entry->source, entry->destination) != 0) {
            bool exists = false;
            if (photoc_fs_exists(entry->destination, &exists) != 0) {
                entry->system_errno = errno;
            } else if (exists) {
                entry->destination_exists = true;
            }
        }
    }
}

static int compare_destination(const void *left, const void *right)
{
    const rename_entry *const *a = left;
    const rename_entry *const *b = right;
    int folded =
        photoc_fs_compare_casefold((*a)->destination, (*b)->destination);
    return folded == 0 ? strcmp((*a)->destination, (*b)->destination) : folded;
}

static int mark_duplicate_destinations(rename_plan *plan)
{
    if (plan->count == 0) {
        return 0;
    }
    if (plan->count > SIZE_MAX / sizeof(rename_entry *)) {
        errno = EOVERFLOW;
        return -1;
    }
    rename_entry **sorted = malloc(plan->count * sizeof(*sorted));
    if (sorted == NULL) {
        return -1;
    }
    size_t count = 0;
    for (size_t i = 0; i < plan->count; ++i) {
        if (plan->entries[i].destination != NULL) {
            sorted[count++] = &plan->entries[i];
        }
    }
    qsort(sorted, count, sizeof(*sorted), compare_destination);
    for (size_t i = 1; i < count; ++i) {
        if (photoc_fs_compare_casefold(sorted[i - 1]->destination,
                                       sorted[i]->destination) == 0) {
            sorted[i - 1]->duplicate_destination = true;
            sorted[i]->duplicate_destination = true;
        }
    }
    free(sorted);
    return 0;
}

static void placeholder_text(const char *format, size_t offset, char *buffer,
                             size_t size)
{
    if (size == 0) {
        return;
    }
    buffer[0] = '\0';
    if (offset == SIZE_MAX || format[offset] != '{') {
        snprintf(buffer, size, "placeholder");
        return;
    }
    const char *close = strchr(format + offset + 1, '}');
    if (close == NULL) {
        snprintf(buffer, size, "placeholder");
        return;
    }
    size_t length = (size_t)(close - (format + offset)) + 1;
    if (length >= size) {
        length = size - 1;
    }
    memcpy(buffer, format + offset, length);
    buffer[length] = '\0';
}

static void print_placeholder(FILE *stream, const char *format, size_t offset)
{
    char text[64];
    placeholder_text(format, offset, text, sizeof(text));
    fputs(text, stream);
}

static bool entry_blocked(const rename_entry *entry)
{
    return entry->metadata_error != PHOTOC_METADATA_OK ||
           entry->template_error != PHOTOC_TEMPLATE_OK ||
           entry->duplicate_destination || entry->destination_exists ||
           entry->source_errno != 0 || entry->source_changed ||
           entry->system_errno != 0;
}

static rename_summary summarize_plan(const rename_plan *plan)
{
    rename_summary summary = {0};
    for (size_t i = 0; i < plan->count; ++i) {
        const rename_entry *entry = &plan->entries[i];
        if (entry_blocked(entry)) {
            ++summary.blocked;
        } else if (strcmp(entry->source, entry->destination) == 0) {
            ++summary.unchanged;
        } else {
            ++summary.planned;
        }
    }
    return summary;
}

static void print_summary(const rename_plan *plan,
                          const rename_summary *summary,
                          const photoc_output *output)
{
    photoc_output_info(
        output,
        "Summary: %zu %s, %zu planned, %zu unchanged, %zu blocked, "
        "%zu applied, %zu rolled back\n",
        plan->count, plan->arw_count == 0 ? "JPEG" : "photos", summary->planned,
        summary->unchanged, summary->blocked, summary->applied,
        summary->rolled_back);
}

static void print_plan(const rename_plan *plan, const char *root,
                       const char *format, bool show_safe)
{
    for (size_t i = 0; i < plan->count; ++i) {
        const rename_entry *entry = &plan->entries[i];
        const char *source = photoc_fs_relative(root, entry->source);
        if (entry->metadata_error != PHOTOC_METADATA_OK) {
            photoc_error_metadata("rename", PHOTOC_ERR_NOTE_SKIPPED, source,
                                  entry->metadata_error, entry->system_errno);
        } else if (entry->template_error != PHOTOC_TEMPLATE_OK) {
            if (entry->template_error == PHOTOC_TEMPLATE_MISSING_VALUE) {
                char placeholder[64];
                placeholder_text(format, entry->template_offset, placeholder,
                                 sizeof(placeholder));
                photoc_error_reportf("rename", PHOTOC_ERR_NOTE_SKIPPED,
                                     PHOTOC_ERR_METADATA, source, 0,
                                     "missing or invalid metadata for %s",
                                     placeholder);
            } else {
                photoc_error_report(
                    "rename", PHOTOC_ERR_NOTE_SKIPPED,
                    photoc_error_kind_for_template(entry->template_error),
                    source,
                    photoc_template_result_message(entry->template_error), 0);
            }
        } else if (entry->duplicate_destination) {
            photoc_error_reportf("rename", PHOTOC_ERR_NOTE_SKIPPED,
                                 PHOTOC_ERR_COLLISION, source, 0,
                                 "duplicate destination '%s'",
                                 photoc_fs_relative(root, entry->destination));
        } else if (entry->destination_exists) {
            photoc_error_reportf("rename", PHOTOC_ERR_NOTE_SKIPPED,
                                 PHOTOC_ERR_COLLISION, source, 0,
                                 "destination exists '%s'",
                                 photoc_fs_relative(root, entry->destination));
        } else if (entry->system_errno != 0) {
            photoc_error_report(
                "rename", PHOTOC_ERR_NOTE_SKIPPED, PHOTOC_ERR_IO, source,
                "cannot check destination", entry->system_errno);
        } else if (entry->source_errno != 0) {
            photoc_error_report("rename", PHOTOC_ERR_NOTE_SKIPPED,
                                PHOTOC_ERR_IO, source, "cannot check source",
                                entry->source_errno);
        } else if (entry->source_changed) {
            photoc_error_report("rename", PHOTOC_ERR_NOTE_SKIPPED,
                                PHOTOC_ERR_IO, source,
                                "source changed during preflight", 0);
        } else if (show_safe) {
            const char *destination =
                photoc_fs_relative(root, entry->destination);
            printf("%s -> %s", source, destination);
            if (strcmp(entry->source, entry->destination) == 0) {
                fputs(" (unchanged)", stdout);
            }
            fputc('\n', stdout);
        }
    }
}

static int source_matches(const rename_entry *entry, const char *path)
{
    struct stat source_info;
    if (lstat(path, &source_info) != 0) {
        return -1;
    }
    if (!S_ISREG(source_info.st_mode) ||
        source_info.st_dev != entry->source_device ||
        source_info.st_ino != entry->source_inode) {
        errno = ESTALE;
        return -1;
    }
    return 0;
}

static void rollback_applied(rename_plan *plan, const char *root,
                             rename_summary *summary)
{
    for (size_t i = plan->count; i > 0; --i) {
        rename_entry *entry = &plan->entries[i - 1];
        if (!entry->applied) {
            continue;
        }
        if (source_matches(entry, entry->destination) == 0 &&
            photoc_fs_rename_noreplace(entry->destination, entry->source) ==
                0) {
            entry->applied = false;
            --summary->applied;
            ++summary->rolled_back;
        } else {
            photoc_error_reportf("rename", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                 NULL, errno,
                                 "rollback failed for '%s' -> '%s'",
                                 photoc_fs_relative(root, entry->destination),
                                 photoc_fs_relative(root, entry->source));
        }
    }
}

static int apply_plan(rename_plan *plan, const char *root,
                      rename_summary *summary)
{
    for (size_t i = 0; i < plan->count; ++i) {
        if (photoc_progress_interrupted()) {
            photoc_progress_before_diagnostic(plan->progress);
            rollback_applied(plan, root, summary);
            return PHOTOC_EXIT_FAILURE;
        }
        rename_entry *entry = &plan->entries[i];
        if (strcmp(entry->source, entry->destination) == 0) {
            continue;
        }
        if (source_matches(entry, entry->source) != 0 ||
            photoc_fs_rename_noreplace(entry->source, entry->destination) !=
                0) {
            int saved_errno = errno;
            photoc_progress_before_diagnostic(plan->progress);
            photoc_error_reportf("rename", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                 NULL, saved_errno,
                                 "apply failed for '%s' -> '%s'",
                                 photoc_fs_relative(root, entry->source),
                                 photoc_fs_relative(root, entry->destination));
            rollback_applied(plan, root, summary);
            return PHOTOC_EXIT_FAILURE;
        }
        entry->applied = true;
        ++summary->applied;
        photoc_progress_increment(plan->progress);
    }
    if (photoc_progress_interrupted()) {
        photoc_progress_before_diagnostic(plan->progress);
        rollback_applied(plan, root, summary);
        return PHOTOC_EXIT_FAILURE;
    }
    return PHOTOC_EXIT_SUCCESS;
}

int photoc_command_rename_with_output(const char *directory, const char *format,
                                      bool recursive, bool apply,
                                      const photoc_output *output)
{
    photoc_output_verbose(
        output, "rename", "input: '%s'; mode: %s; recursion: %s\n", directory,
        apply ? "apply" : "preview", recursive ? "enabled" : "disabled");
    size_t template_offset = SIZE_MAX;
    photoc_template_result validation =
        photoc_filename_template_validate(format, &template_offset);
    if (validation != PHOTOC_TEMPLATE_OK) {
        fprintf(stderr, "photoc rename: %s",
                photoc_template_result_message(validation));
        if (validation == PHOTOC_TEMPLATE_UNKNOWN_PLACEHOLDER) {
            fputs(" ", stderr);
            print_placeholder(stderr, format, template_offset);
        } else if (template_offset != SIZE_MAX) {
            fprintf(stderr, " at byte %zu", template_offset);
        }
        fputc('\n', stderr);
        return PHOTOC_EXIT_USAGE;
    }

    photoc_progress *progress = output == NULL ? NULL : output->progress;
    rename_plan plan = {.output = output, .progress = progress};
    photoc_progress_set_message(progress, "Discovering photos...");
    photoc_progress_start(progress);
    int walk_result =
        recursive ? photoc_fs_walk_recursive(directory, collect_photo, &plan)
                  : photoc_fs_walk(directory, collect_photo, &plan);
    if (walk_result != 0) {
        photoc_progress_fail(progress, "Failed to scan directory");
        int saved_errno = walk_result == 1 ? plan.error_errno : errno;
        photoc_error_report("rename", PHOTOC_ERR_NOTE_NONE,
                            saved_errno == ENOMEM ? PHOTOC_ERR_INTERNAL
                                                  : PHOTOC_ERR_IO,
                            directory, "unable to read directory", saved_errno);
        free_plan(&plan);
        return PHOTOC_EXIT_FAILURE;
    }

    if (plan.count > 1) {
        qsort(plan.entries, plan.count, sizeof(*plan.entries), compare_source);
    }
    photoc_progress_set_message(progress, "Reading metadata...");
    if (progress != NULL && progress->enabled)
        photoc_progress_set_total(progress, plan.count);
    if (prepare_entries(&plan, format) != 0 ||
        mark_duplicate_destinations(&plan) != 0) {
        int saved_errno = errno;
        photoc_progress_fail(progress, "Failed to prepare rename plan");
        photoc_error_report("rename", PHOTOC_ERR_NOTE_NONE,
                            saved_errno == ENOMEM ? PHOTOC_ERR_INTERNAL
                                                  : PHOTOC_ERR_IO,
                            NULL, "unable to prepare plan", saved_errno);
        free_plan(&plan);
        return PHOTOC_EXIT_FAILURE;
    }
    if (photoc_progress_interrupted()) {
        photoc_progress_warn(progress, "Interrupted");
        free_plan(&plan);
        return PHOTOC_EXIT_FAILURE;
    }
    if (apply) {
        recheck_entries(&plan);
    }
    size_t metadata_failures = 0;
    for (size_t i = 0; i < plan.count; ++i) {
        if (plan.entries[i].metadata_error != PHOTOC_METADATA_OK) {
            ++metadata_failures;
        }
    }
    rename_summary summary = summarize_plan(&plan);
    photoc_output_verbose(output, "rename", "ARW files discovered: %zu\n",
                          plan.arw_count);
    photoc_output_verbose(output, "rename",
                          "JPEG files discovered: %zu; skipped: %zu; metadata "
                          "parse failures: %zu; blocked: %zu\n",
                          plan.count - plan.arw_count, plan.skipped,
                          metadata_failures, summary.blocked);
    int exit_code =
        summary.blocked == 0 ? PHOTOC_EXIT_SUCCESS : PHOTOC_EXIT_FAILURE;
    if (!apply) {
        photoc_progress_finish(progress, "Prepared rename plan");
        print_plan(&plan, directory, format, true);
    } else if (summary.blocked != 0) {
        photoc_progress_warn(progress, "Rename preflight blocked");
        print_plan(&plan, directory, format, false);
        fputs("photoc rename: preflight failed; no files changed\n", stderr);
    } else {
        photoc_progress_set_message(progress, "Renaming...");
        if (progress != NULL && progress->enabled)
            photoc_progress_set_total(progress, summary.planned);
        exit_code = apply_plan(&plan, directory, &summary);
        if (exit_code == PHOTOC_EXIT_SUCCESS)
            photoc_progress_finish(progress, "Renamed photos");
        else
            photoc_progress_fail(progress, "Failed to rename photos");
        if (exit_code == PHOTOC_EXIT_SUCCESS) {
            print_plan(&plan, directory, format, true);
        }
    }
    photoc_output_verbose(
        output, "rename", "planned: %zu; applied: %zu; rolled back: %zu\n",
        summary.planned, summary.applied, summary.rolled_back);
    print_summary(&plan, &summary, output);
    free_plan(&plan);
    if (ferror(stdout) || ferror(stderr)) {
        return photoc_error_report("rename", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_IO, NULL,
                                   "unable to write output", EIO);
    }
    return exit_code;
}

int photoc_command_rename(const char *directory, const char *format,
                          bool recursive, bool apply)
{
    return photoc_command_rename_with_output(directory, format, recursive,
                                             apply, NULL);
}
