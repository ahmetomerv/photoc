#if defined(__linux__)
#define _GNU_SOURCE
#elif defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/commands.h"

#include "photoc/error.h"
#include "photoc/exit_codes.h"
#include "photoc/fs.h"
#include "photoc/photo.h"
#include "photoc/session.h"
#include "photoc/timestamp.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
    char *source;
    char *destination;
    char *folder; /* Relative to the sort root; owned by this entry. */
    char *capture_timestamp;
    photoc_metadata_result metadata_error;
    int system_errno;
    bool missing_date;
    bool parent_conflict;
    bool destination_exists;
    bool duplicate_destination;
    int source_errno;
    dev_t source_device;
    ino_t source_inode;
    bool source_changed;
    bool applied;
} sort_entry;

typedef struct {
    sort_entry *entries;
    size_t count;
    size_t capacity;
    int error_errno;
} sort_plan;

typedef struct {
    size_t planned;
    size_t unchanged;
    size_t blocked;
    size_t applied;
    size_t rolled_back;
} sort_summary;

typedef struct {
    char **paths; /* Relative paths owned by this list. */
    size_t count;
    size_t capacity;
} created_directories;

static void free_plan(sort_plan *plan)
{
    for (size_t i = 0; i < plan->count; ++i) {
        free(plan->entries[i].source);
        free(plan->entries[i].destination);
        free(plan->entries[i].folder);
        free(plan->entries[i].capture_timestamp);
    }
    free(plan->entries);
    *plan = (sort_plan){0};
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
        sort_entry *entries =
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
    plan->entries[plan->count++] = (sort_entry){.source = copy};
    return true;
}

static int compare_source(const void *left, const void *right)
{
    const sort_entry *a = left;
    const sort_entry *b = right;
    return strcmp(a->source, b->source);
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
    size_t length = strlen(relative_folder);
    entry->folder = malloc(length + 1);
    if (entry->folder == NULL) {
        return -1;
    }
    memcpy(entry->folder, relative_folder, length + 1);
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
    int comparison = strcmp((*a)->capture_timestamp, (*b)->capture_timestamp);
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
        count > SIZE_MAX / sizeof(Photo) || count > SIZE_MAX / sizeof(size_t)) {
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
    photoc_session_result grouping =
        photoc_session_group(photos, count, gap_minutes, ids);
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
    int folded =
        photoc_fs_compare_casefold((*a)->destination, (*b)->destination);
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

static bool entry_blocked(const sort_entry *entry)
{
    return entry->metadata_error != PHOTOC_METADATA_OK || entry->missing_date ||
           entry->parent_conflict || entry->system_errno != 0 ||
           entry->destination_exists || entry->duplicate_destination ||
           entry->source_errno != 0 || entry->source_changed;
}

static void recheck_entries(sort_plan *plan, const char *root)
{
    for (size_t i = 0; i < plan->count; ++i) {
        sort_entry *entry = &plan->entries[i];
        if (entry->destination == NULL) {
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
        if (entry->parent_conflict || entry->system_errno != 0) {
            continue;
        }
        if (check_parents(root, entry->folder, &entry->parent_conflict) != 0) {
            entry->system_errno = errno;
            continue;
        }
        bool exists = false;
        if (photoc_fs_exists(entry->destination, &exists) != 0) {
            entry->system_errno = errno;
        } else if (exists && strcmp(entry->source, entry->destination) != 0) {
            entry->destination_exists = true;
        }
    }
}

static sort_summary summarize_plan(const sort_plan *plan)
{
    sort_summary summary = {0};
    for (size_t i = 0; i < plan->count; ++i) {
        const sort_entry *entry = &plan->entries[i];
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

static void print_summary(const sort_plan *plan, const sort_summary *summary)
{
    printf("Summary: %zu JPEG, %zu planned, %zu unchanged, %zu blocked, "
           "%zu applied, %zu rolled back\n",
           plan->count, summary->planned, summary->unchanged, summary->blocked,
           summary->applied, summary->rolled_back);
}

static void print_plan(const sort_plan *plan, const char *root, bool show_safe)
{
    for (size_t i = 0; i < plan->count; ++i) {
        const sort_entry *entry = &plan->entries[i];
        const char *source = photoc_fs_relative(root, entry->source);
        if (entry->source_errno != 0) {
            photoc_error_report("sort", PHOTOC_ERR_NOTE_SKIPPED, PHOTOC_ERR_IO,
                                source, "cannot check source",
                                entry->source_errno);
        } else if (entry->source_changed) {
            photoc_error_report("sort", PHOTOC_ERR_NOTE_SKIPPED, PHOTOC_ERR_IO,
                                source, "source changed during preflight", 0);
        } else if (entry->metadata_error != PHOTOC_METADATA_OK) {
            photoc_error_metadata("sort", PHOTOC_ERR_NOTE_SKIPPED, source,
                                  entry->metadata_error, entry->system_errno);
        } else if (entry->missing_date) {
            photoc_error_report("sort", PHOTOC_ERR_NOTE_SKIPPED,
                                PHOTOC_ERR_METADATA, source,
                                "missing or invalid EXIF capture date", 0);
        } else if (entry->parent_conflict) {
            photoc_error_report("sort", PHOTOC_ERR_NOTE_SKIPPED,
                                PHOTOC_ERR_COLLISION, source,
                                "destination parent is not a directory", 0);
        } else if (entry->system_errno != 0) {
            photoc_error_report("sort", PHOTOC_ERR_NOTE_SKIPPED, PHOTOC_ERR_IO,
                                source, "cannot check destination",
                                entry->system_errno);
        } else if (entry->duplicate_destination) {
            photoc_error_reportf("sort", PHOTOC_ERR_NOTE_SKIPPED,
                                 PHOTOC_ERR_COLLISION, source, 0,
                                 "duplicate destination '%s'",
                                 photoc_fs_relative(root, entry->destination));
        } else if (entry->destination_exists) {
            photoc_error_reportf("sort", PHOTOC_ERR_NOTE_SKIPPED,
                                 PHOTOC_ERR_COLLISION, source, 0,
                                 "destination exists '%s'",
                                 photoc_fs_relative(root, entry->destination));
        } else if (show_safe) {
            printf("%s -> %s", source,
                   photoc_fs_relative(root, entry->destination));
            if (strcmp(entry->source, entry->destination) == 0) {
                fputs(" (unchanged)", stdout);
            }
            fputc('\n', stdout);
        }
    }
}

/* Walk one relative directory at a time using pinned descriptors. Neither
   existing nor newly created symlinks can become a destination directory. */
static int open_directory_chain(int root_fd, const char *relative, bool create,
                                created_directories *created)
{
    int current = dup(root_fd);
    if (current < 0 || relative[0] == '\0') {
        return current;
    }
    const char *component = relative;
    for (const char *cursor = relative;; ++cursor) {
        if (*cursor != '/' && *cursor != '\0') {
            continue;
        }
        size_t length = (size_t)(cursor - component);
        if (length == 0 || (length == 1 && component[0] == '.') ||
            (length == 2 && component[0] == '.' && component[1] == '.')) {
            errno = EINVAL;
            break;
        }
        char *name = malloc(length + 1);
        if (name == NULL) {
            break;
        }
        memcpy(name, component, length);
        name[length] = '\0';
        int next = openat(current, name,
                          O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0 && errno == ENOENT && create) {
            size_t prefix_length = (size_t)(cursor - relative);
            char *prefix = malloc(prefix_length + 1);
            if (prefix == NULL) {
                free(name);
                break;
            }
            memcpy(prefix, relative, prefix_length);
            prefix[prefix_length] = '\0';
            if (created->count == created->capacity) {
                free(prefix);
                free(name);
                errno = EOVERFLOW;
                break;
            }
            if (mkdirat(current, name, 0777) == 0) {
                created->paths[created->count++] = prefix;
            } else if (errno == EEXIST) {
                free(prefix);
            } else {
                int saved_errno = errno;
                free(prefix);
                free(name);
                errno = saved_errno;
                break;
            }
            next = openat(current, name,
                          O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        }
        int saved_errno = errno;
        free(name);
        if (next < 0) {
            errno = saved_errno;
            break;
        }
        close(current);
        current = next;
        if (*cursor == '\0') {
            return current;
        }
        component = cursor + 1;
    }
    int saved_errno = errno;
    close(current);
    errno = saved_errno;
    return -1;
}

static int open_parent(int root_fd, const char *relative, const char **name)
{
    const char *slash = strrchr(relative, '/');
    *name = slash == NULL ? relative : slash + 1;
    if (slash == NULL) {
        return dup(root_fd);
    }
    size_t length = (size_t)(slash - relative);
    char *parent = malloc(length + 1);
    if (parent == NULL) {
        return -1;
    }
    memcpy(parent, relative, length);
    parent[length] = '\0';
    int fd = open_directory_chain(root_fd, parent, false, NULL);
    free(parent);
    return fd;
}

static void recheck_source_descriptors(sort_plan *plan, const char *root,
                                       int root_fd)
{
    for (size_t i = 0; i < plan->count; ++i) {
        sort_entry *entry = &plan->entries[i];
        if (entry->destination == NULL) {
            continue;
        }
        const char *name;
        int parent_fd = open_parent(
            root_fd, photoc_fs_relative(root, entry->source), &name);
        if (parent_fd < 0) {
            entry->source_errno = errno;
            continue;
        }
        struct stat source_info;
        if (fstatat(parent_fd, name, &source_info, AT_SYMLINK_NOFOLLOW) != 0) {
            entry->source_errno = errno;
        } else if (!S_ISREG(source_info.st_mode) ||
                   source_info.st_dev != entry->source_device ||
                   source_info.st_ino != entry->source_inode) {
            entry->source_changed = true;
        }
        close(parent_fd);
    }
}

static int prepare_created_list(const sort_plan *plan,
                                created_directories *created)
{
    size_t capacity = 0;
    for (size_t i = 0; i < plan->count; ++i) {
        const sort_entry *entry = &plan->entries[i];
        if (entry_blocked(entry) ||
            strcmp(entry->source, entry->destination) == 0) {
            continue;
        }
        size_t parts = 1;
        for (const char *p = entry->folder; *p != '\0'; ++p) {
            if (*p == '/') {
                ++parts;
            }
        }
        if (capacity > SIZE_MAX - parts) {
            errno = EOVERFLOW;
            return -1;
        }
        capacity += parts;
    }
    if (capacity > SIZE_MAX / sizeof(*created->paths)) {
        errno = EOVERFLOW;
        return -1;
    }
    if (capacity != 0) {
        created->paths = malloc(capacity * sizeof(*created->paths));
        if (created->paths == NULL) {
            return -1;
        }
    }
    created->capacity = capacity;
    return 0;
}

static void remove_created_directories(int root_fd,
                                       created_directories *created)
{
    for (size_t i = created->count; i > 0; --i) {
        const char *name;
        int parent_fd = open_parent(root_fd, created->paths[i - 1], &name);
        if (parent_fd < 0 || unlinkat(parent_fd, name, AT_REMOVEDIR) != 0) {
            photoc_error_reportf("sort", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                 NULL, errno,
                                 "unable to remove created directory '%s'",
                                 created->paths[i - 1]);
        }
        if (parent_fd >= 0) {
            close(parent_fd);
        }
    }
}

static void free_created_list(created_directories *created)
{
    for (size_t i = 0; i < created->count; ++i) {
        free(created->paths[i]);
    }
    free(created->paths);
}

static int move_entry(int root_fd, const char *root, sort_entry *entry,
                      bool reverse)
{
    const char *source_path = reverse ? entry->destination : entry->source;
    const char *destination_path = reverse ? entry->source : entry->destination;
    const char *source_name;
    const char *destination_name;
    int source_fd = open_parent(root_fd, photoc_fs_relative(root, source_path),
                                &source_name);
    if (source_fd < 0) {
        return -1;
    }
    int destination_fd = open_parent(
        root_fd, photoc_fs_relative(root, destination_path), &destination_name);
    if (destination_fd < 0) {
        int saved_errno = errno;
        close(source_fd);
        errno = saved_errno;
        return -1;
    }
    struct stat source_info;
    int result =
        fstatat(source_fd, source_name, &source_info, AT_SYMLINK_NOFOLLOW);
    if (result == 0 && (!S_ISREG(source_info.st_mode) ||
                        source_info.st_dev != entry->source_device ||
                        source_info.st_ino != entry->source_inode)) {
        errno = ESTALE;
        result = -1;
    }
    if (result == 0) {
        result = photoc_fs_renameat_noreplace(source_fd, source_name,
                                              destination_fd, destination_name);
    }
    int saved_errno = errno;
    close(source_fd);
    close(destination_fd);
    errno = saved_errno;
    return result;
}

static void rollback_moves(sort_plan *plan, const char *root, int root_fd,
                           sort_summary *summary)
{
    for (size_t i = plan->count; i > 0; --i) {
        sort_entry *entry = &plan->entries[i - 1];
        if (!entry->applied) {
            continue;
        }
        if (move_entry(root_fd, root, entry, true) == 0) {
            entry->applied = false;
            --summary->applied;
            ++summary->rolled_back;
        } else {
            photoc_error_reportf("sort", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                 NULL, errno,
                                 "rollback failed for '%s' -> '%s'",
                                 photoc_fs_relative(root, entry->destination),
                                 photoc_fs_relative(root, entry->source));
        }
    }
}

static int apply_plan(sort_plan *plan, const char *root, int root_fd,
                      sort_summary *summary)
{
    created_directories created = {0};
    if (prepare_created_list(plan, &created) != 0) {
        photoc_error_report("sort", PHOTOC_ERR_NOTE_NONE,
                            errno == ENOMEM ? PHOTOC_ERR_INTERNAL
                                            : PHOTOC_ERR_IO,
                            NULL, "unable to prepare directories", errno);
        return PHOTOC_EXIT_FAILURE;
    }
    for (size_t i = 0; i < plan->count; ++i) {
        sort_entry *entry = &plan->entries[i];
        if (strcmp(entry->source, entry->destination) == 0) {
            continue;
        }
        int fd = open_directory_chain(root_fd, entry->folder, true, &created);
        if (fd < 0) {
            photoc_error_reportf(
                "sort", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO, NULL, errno,
                "unable to create destination directory '%s'", entry->folder);
            remove_created_directories(root_fd, &created);
            free_created_list(&created);
            return PHOTOC_EXIT_FAILURE;
        }
        close(fd);
    }
    int result = PHOTOC_EXIT_SUCCESS;
    for (size_t i = 0; i < plan->count; ++i) {
        sort_entry *entry = &plan->entries[i];
        if (strcmp(entry->source, entry->destination) == 0) {
            continue;
        }
        if (move_entry(root_fd, root, entry, false) != 0) {
            photoc_error_reportf("sort", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                 NULL, errno, "apply failed for '%s' -> '%s'",
                                 photoc_fs_relative(root, entry->source),
                                 photoc_fs_relative(root, entry->destination));
            rollback_moves(plan, root, root_fd, summary);
            remove_created_directories(root_fd, &created);
            result = PHOTOC_EXIT_FAILURE;
            break;
        }
        entry->applied = true;
        ++summary->applied;
    }
    free_created_list(&created);
    return result;
}

int photoc_command_sort(const char *directory, bool recursive,
                        photoc_sort_mode mode, uint32_t gap_minutes, bool apply)
{
    struct stat root_before;
    if (apply && lstat(directory, &root_before) != 0) {
        return photoc_error_report("sort", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                   directory, "unable to inspect directory",
                                   errno);
    }
    sort_plan plan = {0};
    int walk_result =
        recursive ? photoc_fs_walk_recursive(directory, collect_jpeg, &plan)
                  : photoc_fs_walk(directory, collect_jpeg, &plan);
    if (walk_result != 0) {
        int saved_errno = walk_result == 1 ? plan.error_errno : errno;
        photoc_error_report("sort", PHOTOC_ERR_NOTE_NONE,
                            saved_errno == ENOMEM ? PHOTOC_ERR_INTERNAL
                                                  : PHOTOC_ERR_IO,
                            directory, "unable to read directory", saved_errno);
        free_plan(&plan);
        return PHOTOC_EXIT_FAILURE;
    }
    if (plan.count > 1) {
        qsort(plan.entries, plan.count, sizeof(*plan.entries), compare_source);
    }
    if (load_entries(&plan) != 0 ||
        (mode == PHOTOC_SORT_BY_DATE
             ? prepare_date_destinations(&plan, directory)
             : prepare_session_destinations(&plan, directory, gap_minutes)) !=
            0 ||
        mark_duplicates(&plan) != 0) {
        int saved_errno = errno;
        photoc_error_report("sort", PHOTOC_ERR_NOTE_NONE,
                            saved_errno == ENOMEM ? PHOTOC_ERR_INTERNAL
                                                  : PHOTOC_ERR_IO,
                            NULL, "unable to prepare plan", saved_errno);
        free_plan(&plan);
        return PHOTOC_EXIT_FAILURE;
    }

    int root_fd = -1;
    if (apply) {
        root_fd =
            open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (root_fd < 0) {
            photoc_error_report("sort", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                directory, "cannot open directory", errno);
            free_plan(&plan);
            return PHOTOC_EXIT_FAILURE;
        }
        struct stat root_now;
        if (fstat(root_fd, &root_now) != 0 ||
            root_before.st_dev != root_now.st_dev ||
            root_before.st_ino != root_now.st_ino) {
            photoc_error_report(
                "sort", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO, NULL,
                "directory changed during preflight; no files changed", 0);
            close(root_fd);
            free_plan(&plan);
            return PHOTOC_EXIT_FAILURE;
        }
        recheck_entries(&plan, directory);
        recheck_source_descriptors(&plan, directory, root_fd);
    }
    sort_summary summary = summarize_plan(&plan);
    int exit_code =
        summary.blocked == 0 ? PHOTOC_EXIT_SUCCESS : PHOTOC_EXIT_FAILURE;
    if (!apply) {
        print_plan(&plan, directory, true);
    } else if (summary.blocked != 0) {
        print_plan(&plan, directory, false);
        fputs("photoc sort: preflight failed; no files changed\n", stderr);
    } else {
        exit_code = apply_plan(&plan, directory, root_fd, &summary);
        if (exit_code == PHOTOC_EXIT_SUCCESS) {
            print_plan(&plan, directory, true);
        }
    }
    print_summary(&plan, &summary);
    if (root_fd >= 0) {
        close(root_fd);
    }
    free_plan(&plan);
    if (ferror(stdout) || ferror(stderr)) {
        return photoc_error_report("sort", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                   NULL, "unable to write output", EIO);
    }
    return exit_code;
}
