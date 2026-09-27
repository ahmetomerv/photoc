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

typedef struct {
    char *source;
    char *destination;
    photoc_metadata_result metadata_error;
    photoc_template_result template_error;
    size_t template_offset;
    int system_errno;
    bool destination_exists;
    bool duplicate_destination;
} rename_entry;

typedef struct {
    rename_entry *entries;
    size_t count;
    size_t capacity;
    int error_errno;
} rename_plan;

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

static const char *relative_name(const char *root, const char *path)
{
    size_t root_length = strlen(root);
    while (root_length > 1 && root[root_length - 1] == '/') {
        --root_length;
    }
    const char *relative = path + root_length;
    while (*relative == '/') {
        ++relative;
    }
    return relative;
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

        bool exists = false;
        if (photoc_fs_exists(entry->destination, &exists) != 0) {
            entry->system_errno = errno;
        } else if (exists && strcmp(entry->source, entry->destination) != 0) {
            entry->destination_exists = true;
        }
    }
    return 0;
}

static unsigned char ascii_lower(unsigned char byte)
{
    return byte >= 'A' && byte <= 'Z' ? (unsigned char)(byte - 'A' + 'a') : byte;
}

static int compare_folded_paths(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        unsigned char left = ascii_lower((unsigned char)*a++);
        unsigned char right = ascii_lower((unsigned char)*b++);
        if (left != right) {
            return left < right ? -1 : 1;
        }
    }
    if (*a == *b) {
        return 0;
    }
    return *a == '\0' ? -1 : 1;
}

static int compare_destination(const void *left, const void *right)
{
    const rename_entry *const *a = left;
    const rename_entry *const *b = right;
    int folded = compare_folded_paths((*a)->destination, (*b)->destination);
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
        if (compare_folded_paths(sorted[i - 1]->destination,
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

static int print_plan(const rename_plan *plan, const char *root,
                      const char *format)
{
    int exit_code = PHOTOC_EXIT_SUCCESS;
    for (size_t i = 0; i < plan->count; ++i) {
        const rename_entry *entry = &plan->entries[i];
        const char *source = relative_name(root, entry->source);
        if (entry->metadata_error != PHOTOC_METADATA_OK) {
            fprintf(stderr, "photoc rename: %s: skipped: %s", source,
                    photo_metadata_result_message(entry->metadata_error));
            if (entry->metadata_error == PHOTOC_METADATA_IO_ERROR) {
                fprintf(stderr, ": %s", strerror(entry->system_errno));
            }
            fputc('\n', stderr);
        } else if (entry->template_error != PHOTOC_TEMPLATE_OK) {
            fprintf(stderr, "photoc rename: %s: skipped: ", source);
            if (entry->template_error == PHOTOC_TEMPLATE_MISSING_VALUE) {
                fputs("missing or invalid metadata for ", stderr);
                print_placeholder(stderr, format, entry->template_offset);
            } else {
                fputs(photoc_template_result_message(entry->template_error),
                      stderr);
            }
            fputc('\n', stderr);
        } else if (entry->duplicate_destination) {
            fprintf(stderr, "photoc rename: %s: skipped: duplicate destination '%s'\n",
                    source, relative_name(root, entry->destination));
        } else if (entry->destination_exists) {
            fprintf(stderr, "photoc rename: %s: skipped: destination exists '%s'\n",
                    source, relative_name(root, entry->destination));
        } else if (entry->system_errno != 0) {
            fprintf(stderr, "photoc rename: %s: skipped: cannot check destination: %s\n",
                    source, strerror(entry->system_errno));
        } else {
            const char *destination = relative_name(root, entry->destination);
            printf("%s -> %s", source, destination);
            if (strcmp(entry->source, entry->destination) == 0) {
                fputs(" (unchanged)", stdout);
            }
            fputc('\n', stdout);
            continue;
        }
        exit_code = PHOTOC_EXIT_FAILURE;
    }
    return exit_code;
}

int photoc_command_rename(const char *directory, const char *format,
                          bool recursive)
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
    int exit_code = print_plan(&plan, directory, format);
    free_plan(&plan);
    if (ferror(stdout) || ferror(stderr)) {
        fputs("photoc rename: unable to write output\n", stderr);
        return PHOTOC_EXIT_FAILURE;
    }
    return exit_code;
}
