#define _POSIX_C_SOURCE 200809L

#include "photoc/commands.h"

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

static bool collect_jpeg(const char *path, photoc_fs_type type,
                         void *user_data)
{
    rename_plan *plan = user_data;
    if (type != PHOTOC_FS_FILE || !photoc_fs_is_jpeg(path)) {
        return true;
    }
    if (plan->count == plan->capacity) {
        size_t capacity = plan->capacity == 0 ? 16 : plan->capacity * 2;
        if (capacity < plan->capacity ||
            capacity > SIZE_MAX / sizeof(*plan->entries)) {
            plan->error_errno = EOVERFLOW;
            return false;
        }
        rename_entry *entries = realloc(plan->entries,
                                        capacity * sizeof(*entries));
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
    plan->entries[plan->count++] = (rename_entry){.source = copy};
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
    size_t parent_length = slash == NULL ? 1 :
                           slash == source ? 1 : (size_t)(slash - source);
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
            format, &photo, (uint64_t)i + 1, 4, &name,
            &entry->template_offset);
        photo_cleanup(&photo);
        if (entry->template_error != PHOTOC_TEMPLATE_OK) {
            if (entry->template_error == PHOTOC_TEMPLATE_NO_MEMORY ||
                entry->template_error == PHOTOC_TEMPLATE_TOO_LONG) {
                errno = entry->template_error == PHOTOC_TEMPLATE_NO_MEMORY ?
                        ENOMEM : EOVERFLOW;
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
    int folded = photoc_fs_compare_casefold((*a)->destination, (*b)->destination);
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

static void print_placeholder(FILE *stream, const char *format, size_t offset)
{
    if (offset == SIZE_MAX || format[offset] != '{') {
        fputs("placeholder", stream);
        return;
    }
    const char *close = strchr(format + offset + 1, '}');
    if (close == NULL) {
        fputs("placeholder", stream);
        return;
    }
    fwrite(format + offset, 1, (size_t)(close - (format + offset)) + 1,
           stream);
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
                          const rename_summary *summary)
{
    printf("Summary: %zu JPEG, %zu planned, %zu unchanged, %zu blocked, "
           "%zu applied, %zu rolled back\n", plan->count, summary->planned,
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
            fprintf(stderr, "photoc rename: '%s': skipped: %s", source,
                    photo_metadata_result_message(entry->metadata_error));
            if (entry->metadata_error == PHOTOC_METADATA_IO_ERROR) {
                fprintf(stderr, ": %s", strerror(entry->system_errno));
            }
            fputc('\n', stderr);
        } else if (entry->template_error != PHOTOC_TEMPLATE_OK) {
            fprintf(stderr, "photoc rename: '%s': skipped: ", source);
            if (entry->template_error == PHOTOC_TEMPLATE_MISSING_VALUE) {
                fputs("missing or invalid metadata for ", stderr);
                print_placeholder(stderr, format, entry->template_offset);
            } else {
                fputs(photoc_template_result_message(entry->template_error),
                      stderr);
            }
            fputc('\n', stderr);
        } else if (entry->duplicate_destination) {
            fprintf(stderr, "photoc rename: '%s': skipped: duplicate destination '%s'\n",
                    source, photoc_fs_relative(root, entry->destination));
        } else if (entry->destination_exists) {
            fprintf(stderr, "photoc rename: '%s': skipped: destination exists '%s'\n",
                    source, photoc_fs_relative(root, entry->destination));
        } else if (entry->system_errno != 0) {
            fprintf(stderr, "photoc rename: '%s': skipped: cannot check destination: %s\n",
                    source, strerror(entry->system_errno));
        } else if (entry->source_errno != 0) {
            fprintf(stderr, "photoc rename: '%s': skipped: cannot check source: %s\n",
                    source, strerror(entry->source_errno));
        } else if (entry->source_changed) {
            fprintf(stderr, "photoc rename: '%s': skipped: source changed during preflight\n",
                    source);
        } else if (show_safe) {
            const char *destination = photoc_fs_relative(root, entry->destination);
            printf("%s -> %s", source, destination);
            if (strcmp(entry->source, entry->destination) == 0) {
                fputs(" (unchanged)", stdout);
            }
            fputc('\n', stdout);
        }
    }
}

static int source_matches(const rename_entry *entry)
{
    struct stat source_info;
    if (lstat(entry->source, &source_info) != 0) {
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
        if (photoc_fs_rename_noreplace(entry->destination,
                                      entry->source) == 0) {
            entry->applied = false;
            --summary->applied;
            ++summary->rolled_back;
        } else {
            fprintf(stderr, "photoc rename: rollback failed for '%s' -> '%s': %s\n",
                    photoc_fs_relative(root, entry->destination),
                    photoc_fs_relative(root, entry->source), strerror(errno));
        }
    }
}

static int apply_plan(rename_plan *plan, const char *root,
                      rename_summary *summary)
{
    for (size_t i = 0; i < plan->count; ++i) {
        rename_entry *entry = &plan->entries[i];
        if (strcmp(entry->source, entry->destination) == 0) {
            continue;
        }
        if (source_matches(entry) != 0 ||
            photoc_fs_rename_noreplace(entry->source,
                                       entry->destination) != 0) {
            int saved_errno = errno;
            fprintf(stderr, "photoc rename: apply failed for '%s' -> '%s': %s\n",
                    photoc_fs_relative(root, entry->source),
                    photoc_fs_relative(root, entry->destination),
                    strerror(saved_errno));
            rollback_applied(plan, root, summary);
            return PHOTOC_EXIT_FAILURE;
        }
        entry->applied = true;
        ++summary->applied;
    }
    return PHOTOC_EXIT_SUCCESS;
}

int photoc_command_rename(const char *directory, const char *format,
                          bool recursive, bool apply)
{
    size_t template_offset = SIZE_MAX;
    photoc_template_result validation = photoc_filename_template_validate(
        format, &template_offset);
    if (validation != PHOTOC_TEMPLATE_OK) {
        fprintf(stderr, "photoc rename: %s", photoc_template_result_message(
                validation));
        if (validation == PHOTOC_TEMPLATE_UNKNOWN_PLACEHOLDER) {
            fputs(" ", stderr);
            print_placeholder(stderr, format, template_offset);
        } else if (template_offset != SIZE_MAX) {
            fprintf(stderr, " at byte %zu", template_offset);
        }
        fputc('\n', stderr);
        return PHOTOC_EXIT_USAGE;
    }

    rename_plan plan = {0};
    int walk_result = recursive ?
        photoc_fs_walk_recursive(directory, collect_jpeg, &plan) :
        photoc_fs_walk(directory, collect_jpeg, &plan);
    if (walk_result != 0) {
        int saved_errno = walk_result == 1 ? plan.error_errno : errno;
        fprintf(stderr, "photoc rename: '%s': %s\n", directory,
                strerror(saved_errno));
        free_plan(&plan);
        return PHOTOC_EXIT_FAILURE;
    }

    if (plan.count > 1) {
        qsort(plan.entries, plan.count, sizeof(*plan.entries), compare_source);
    }
    if (prepare_entries(&plan, format) != 0 ||
        mark_duplicate_destinations(&plan) != 0) {
        int saved_errno = errno;
        fprintf(stderr, "photoc rename: unable to prepare plan: %s\n",
                strerror(saved_errno));
        free_plan(&plan);
        return PHOTOC_EXIT_FAILURE;
    }
    if (apply) {
        recheck_entries(&plan);
    }
    rename_summary summary = summarize_plan(&plan);
    int exit_code = summary.blocked == 0 ? PHOTOC_EXIT_SUCCESS :
                    PHOTOC_EXIT_FAILURE;
    if (!apply) {
        print_plan(&plan, directory, format, true);
    } else if (summary.blocked != 0) {
        print_plan(&plan, directory, format, false);
        fputs("photoc rename: preflight failed; no files changed\n", stderr);
    } else {
        exit_code = apply_plan(&plan, directory, &summary);
        if (exit_code == PHOTOC_EXIT_SUCCESS) {
            print_plan(&plan, directory, format, true);
        }
    }
    print_summary(&plan, &summary);
    free_plan(&plan);
    if (ferror(stdout) || ferror(stderr)) {
        fputs("photoc rename: unable to write output\n", stderr);
        return PHOTOC_EXIT_FAILURE;
    }
    return exit_code;
}
