#include "photoc/commands.h"

#include "photoc/exit_codes.h"
#include "photoc/fs.h"
#include "photoc/photo.h"
#include "photoc/session.h"
#include "photoc/timestamp.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *source;
    char *destination;
    char *capture_timestamp;
    photoc_metadata_result metadata_error;
    int system_errno;
    bool missing_date;
    bool parent_conflict;
    bool destination_exists;
    bool duplicate_destination;
} sort_entry;

typedef struct {
    sort_entry *entries;
    size_t count;
    size_t capacity;
    int error_errno;
} sort_plan;

static void free_plan(sort_plan *plan)
{
    for (size_t i = 0; i < plan->count; ++i) {
        free(plan->entries[i].source);
        free(plan->entries[i].destination);
        free(plan->entries[i].capture_timestamp);
    }
    free(plan->entries);
}

static bool collect_jpeg(const char *path, photoc_fs_type type, void *user_data)
{
    sort_plan *plan = user_data;
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
        sort_entry *entries = realloc(plan->entries,
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
    plan->entries[plan->count++] = (sort_entry){.source = copy};
    return true;
}

static int compare_source(const void *left, const void *right)
{
    const sort_entry *a = left;
    const sort_entry *b = right;
    return strcmp(a->source, b->source);
}

static const char *relative_name(const char *root, const char *path)
{
    size_t length = strlen(root);
    while (length > 1 && root[length - 1] == '/') {
        --length;
    }
    const char *relative = path + length;
    while (*relative == '/') {
        ++relative;
    }
    return relative;
}

/* A future move must not enter an existing file or symlink in its folder. */
static int check_parents(const char *root, const char *relative_folder,
                         bool *conflict)
{
    *conflict = false;
    size_t length = strlen(relative_folder);
    for (size_t i = 0; i <= length; ++i) {
        if (relative_folder[i] != '/' && relative_folder[i] != '\0') {
            continue;
        }
        char *part = malloc(i + 1);
        if (part == NULL) {
            return -1;
        }
        memcpy(part, relative_folder, i);
        part[i] = '\0';
        char *path = NULL;
        int join_result = photoc_fs_join(root, part, &path);
        free(part);
        if (join_result != 0) {
            return -1;
        }
        photoc_fs_type type;
        int result = photoc_fs_get_type(path, &type);
        int saved_errno = errno;
        free(path);
        if (result == 0 && type != PHOTOC_FS_DIRECTORY) {
            *conflict = true;
            return 0;
        }
        if (result != 0 && saved_errno != ENOENT) {
            errno = saved_errno;
            return -1;
        }
    }
    return 0;
}

static int build_destination(sort_entry *entry, const char *root,
                             const char *relative_folder)
{
    char *folder = NULL;
    char *filename = NULL;
    if (photoc_fs_join(root, relative_folder, &folder) != 0 ||
        photoc_fs_filename(entry->source, &filename) != 0) {
        free(folder);
        free(filename);
        return -1;
    }
    int join_result = photoc_fs_join(folder, filename, &entry->destination);
    free(folder);
    free(filename);
    if (join_result != 0) {
        return -1;
    }
    if (check_parents(root, relative_folder, &entry->parent_conflict) != 0) {
        entry->system_errno = errno;
        return 0;
    }
    if (entry->parent_conflict) {
        return 0;
    }
    bool exists = false;
    if (photoc_fs_exists(entry->destination, &exists) != 0) {
        entry->system_errno = errno;
    } else if (exists && strcmp(entry->source, entry->destination) != 0) {
        entry->destination_exists = true;
    }
    return 0;
}

static int load_entries(sort_plan *plan)
{
    for (size_t i = 0; i < plan->count; ++i) {
        sort_entry *entry = &plan->entries[i];
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
        if (!photoc_timestamp_is_valid(photo.capture_timestamp)) {
            entry->missing_date = true;
            photo_cleanup(&photo);
            continue;
        }
        entry->capture_timestamp = photo.capture_timestamp;
        photo.capture_timestamp = NULL;
        photo_cleanup(&photo);
    }
    return 0;
}

static int prepare_date_destinations(sort_plan *plan, const char *root)
{
    for (size_t i = 0; i < plan->count; ++i) {
        sort_entry *entry = &plan->entries[i];
        if (entry->capture_timestamp == NULL) {
            continue;
        }
        char date_path[11];
        memcpy(date_path, entry->capture_timestamp, 4);
        date_path[4] = '/';
        memcpy(date_path + 5, entry->capture_timestamp + 5, 2);
        date_path[7] = '/';
        memcpy(date_path + 8, entry->capture_timestamp + 8, 2);
        date_path[10] = '\0';
        if (build_destination(entry, root, date_path) != 0) {
            return -1;
        }
    }
    return 0;
}

static int compare_capture(const void *left, const void *right)
{
    const sort_entry *const *a = left;
    const sort_entry *const *b = right;
    int comparison = strcmp((*a)->capture_timestamp,
                            (*b)->capture_timestamp);
    return comparison == 0 ? strcmp((*a)->source, (*b)->source) : comparison;
}

static int prepare_session_destinations(sort_plan *plan, const char *root,
                                        uint32_t gap_minutes)
{
    size_t count = 0;
    for (size_t i = 0; i < plan->count; ++i) {
        if (plan->entries[i].capture_timestamp != NULL) {
            ++count;
        }
    }
    if (count == 0) {
        return 0;
    }
    if (count > SIZE_MAX / sizeof(sort_entry *) ||
        count > SIZE_MAX / sizeof(Photo) ||
        count > SIZE_MAX / sizeof(size_t)) {
        errno = EOVERFLOW;
        return -1;
    }
    sort_entry **dated = malloc(count * sizeof(*dated));
    Photo *photos = malloc(count * sizeof(*photos));
    size_t *ids = malloc(count * sizeof(*ids));
    if (dated == NULL || photos == NULL || ids == NULL) {
        free(dated);
        free(photos);
        free(ids);
        errno = ENOMEM;
        return -1;
    }
    size_t position = 0;
    for (size_t i = 0; i < plan->count; ++i) {
        if (plan->entries[i].capture_timestamp != NULL) {
            dated[position++] = &plan->entries[i];
        }
    }
    qsort(dated, count, sizeof(*dated), compare_capture);
    for (size_t i = 0; i < count; ++i) {
        /* Borrow each entry's timestamp only for the session calculation. */
        photos[i] = (Photo){.capture_timestamp = dated[i]->capture_timestamp};
    }
    photoc_session_result grouping = photoc_session_group(photos, count,
                                                          gap_minutes, ids);
    free(photos);
    if (grouping != PHOTOC_SESSION_OK) {
        free(dated);
        free(ids);
        errno = EINVAL;
        return -1;
    }
    int result = 0;
    for (size_t i = 0; i < count; ++i) {
        char folder[sizeof(size_t) * 3 + 9];
        int length = snprintf(folder, sizeof(folder), "session-%03zu", ids[i]);
        if (length < 0 || (size_t)length >= sizeof(folder)) {
            errno = EOVERFLOW;
            result = -1;
            break;
        }
        if (build_destination(dated[i], root, folder) != 0) {
            result = -1;
            break;
        }
    }
    free(dated);
    free(ids);
    return result;
}

static int compare_destination(const void *left, const void *right)
{
    const sort_entry *const *a = left;
    const sort_entry *const *b = right;
    int folded = photoc_fs_compare_casefold((*a)->destination,
                                            (*b)->destination);
    return folded == 0 ? strcmp((*a)->destination, (*b)->destination) : folded;
}

static int mark_duplicates(sort_plan *plan)
{
    if (plan->count == 0) {
        return 0;
    }
    if (plan->count > SIZE_MAX / sizeof(sort_entry *)) {
        errno = EOVERFLOW;
        return -1;
    }
    sort_entry **sorted = malloc(plan->count * sizeof(*sorted));
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

int photoc_command_sort(const char *directory, bool recursive,
                        photoc_sort_mode mode, uint32_t gap_minutes)
{
    sort_plan plan = {0};
    int walk_result = recursive ?
        photoc_fs_walk_recursive(directory, collect_jpeg, &plan) :
        photoc_fs_walk(directory, collect_jpeg, &plan);
    if (walk_result != 0) {
        int saved_errno = walk_result == 1 ? plan.error_errno : errno;
        fprintf(stderr, "photoc sort: '%s': %s\n", directory,
                strerror(saved_errno));
        free_plan(&plan);
        return PHOTOC_EXIT_FAILURE;
    }
    if (plan.count > 1) {
        qsort(plan.entries, plan.count, sizeof(*plan.entries), compare_source);
    }
    if (load_entries(&plan) != 0 ||
        (mode == PHOTOC_SORT_BY_DATE ?
         prepare_date_destinations(&plan, directory) :
         prepare_session_destinations(&plan, directory, gap_minutes)) != 0 ||
        mark_duplicates(&plan) != 0) {
        int saved_errno = errno;
        fprintf(stderr, "photoc sort: unable to prepare plan: %s\n",
                strerror(saved_errno));
        free_plan(&plan);
        return PHOTOC_EXIT_FAILURE;
    }

    size_t planned = 0;
    size_t unchanged = 0;
    size_t skipped = 0;
    for (size_t i = 0; i < plan.count; ++i) {
        const sort_entry *entry = &plan.entries[i];
        const char *source = relative_name(directory, entry->source);
        if (entry->metadata_error != PHOTOC_METADATA_OK) {
            fprintf(stderr, "photoc sort: %s: skipped: %s", source,
                    photo_metadata_result_message(entry->metadata_error));
            if (entry->metadata_error == PHOTOC_METADATA_IO_ERROR) {
                fprintf(stderr, ": %s", strerror(entry->system_errno));
            }
            fputc('\n', stderr);
            ++skipped;
        } else if (entry->missing_date) {
            fprintf(stderr, "photoc sort: %s: skipped: missing or invalid EXIF capture date\n",
                    source);
            ++skipped;
        } else if (entry->parent_conflict) {
            fprintf(stderr, "photoc sort: %s: skipped: destination parent is not a directory\n",
                    source);
            ++skipped;
        } else if (entry->system_errno != 0) {
            fprintf(stderr, "photoc sort: %s: skipped: cannot check destination: %s\n",
                    source, strerror(entry->system_errno));
            ++skipped;
        } else if (entry->duplicate_destination) {
            fprintf(stderr, "photoc sort: %s: skipped: duplicate destination '%s'\n",
                    source, relative_name(directory, entry->destination));
            ++skipped;
        } else if (entry->destination_exists) {
            fprintf(stderr, "photoc sort: %s: skipped: destination exists '%s'\n",
                    source, relative_name(directory, entry->destination));
            ++skipped;
        } else {
            printf("%s -> %s", source,
                   relative_name(directory, entry->destination));
            if (strcmp(entry->source, entry->destination) == 0) {
                fputs(" (unchanged)", stdout);
                ++unchanged;
            } else {
                ++planned;
            }
            fputc('\n', stdout);
        }
    }
    printf("Summary: %zu JPEG, %zu planned, %zu unchanged, %zu skipped (dry-run)\n",
           plan.count, planned, unchanged, skipped);
    free_plan(&plan);
    if (ferror(stdout) || ferror(stderr)) {
        fputs("photoc sort: unable to write output\n", stderr);
        return PHOTOC_EXIT_FAILURE;
    }
    return skipped == 0 ? PHOTOC_EXIT_SUCCESS : PHOTOC_EXIT_FAILURE;
}
