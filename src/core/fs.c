#if defined(__linux__)
#define _GNU_SOURCE
#elif defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/fs.h"

#include <dirent.h>
#include <errno.h>
#if defined(__linux__)
#include <fcntl.h>
#include <linux/fs.h>
#elif defined(__APPLE__)
#include <sys/stdio.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool valid_path(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return false;
    }
    return true;
}

static int basename_bounds(const char *path, size_t *start, size_t *length)
{
    if (!valid_path(path)) {
        return -1;
    }

    size_t end = strlen(path);
    while (end > 1 && path[end - 1] == '/') {
        --end;
    }

    if (end == 1 && path[0] == '/') {
        *start = 0;
        *length = 1;
        return 0;
    }

    size_t begin = end;
    while (begin > 0 && path[begin - 1] != '/') {
        --begin;
    }
    *start = begin;
    *length = end - begin;
    return 0;
}

/* Returns 1 for an extension, 0 for none, or -1 for an invalid path. */
static int extension_bounds(const char *path, size_t *start, size_t *length)
{
    size_t name_start;
    size_t name_length;
    if (basename_bounds(path, &name_start, &name_length) != 0) {
        return -1;
    }

    size_t end = name_start + name_length;
    for (size_t i = end; i > name_start; --i) {
        if (path[i - 1] == '.') {
            size_t dot = i - 1;
            if (dot > name_start && i < end) {
                *start = i;
                *length = end - i;
                return 1;
            }
            break;
        }
    }
    return 0;
}

static int copy_part(const char *start, size_t length, char **output)
{
    if (length == SIZE_MAX) {
        errno = EOVERFLOW;
        return -1;
    }

    char *copy = malloc(length + 1);
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, start, length);
    copy[length] = '\0';
    *output = copy;
    return 0;
}

int photoc_fs_exists(const char *path, bool *exists)
{
    if (exists == NULL || !valid_path(path)) {
        errno = EINVAL;
        return -1;
    }

    struct stat info;
    if (lstat(path, &info) == 0) {
        *exists = true;
        return 0;
    }
    if (errno == ENOENT || errno == ENOTDIR) {
        *exists = false;
        return 0;
    }
    return -1;
}

int photoc_fs_get_type(const char *path, photoc_fs_type *type)
{
    if (type == NULL || !valid_path(path)) {
        errno = EINVAL;
        return -1;
    }

    struct stat info;
    if (lstat(path, &info) != 0) {
        return -1;
    }
    if (S_ISREG(info.st_mode)) {
        *type = PHOTOC_FS_FILE;
    } else if (S_ISDIR(info.st_mode)) {
        *type = PHOTOC_FS_DIRECTORY;
    } else {
        *type = PHOTOC_FS_OTHER;
    }
    return 0;
}

int photoc_fs_file_size(const char *path, uint64_t *size)
{
    if (size == NULL || !valid_path(path)) {
        errno = EINVAL;
        return -1;
    }

    struct stat info;
    if (lstat(path, &info) != 0) {
        return -1;
    }
    if (!S_ISREG(info.st_mode)) {
        errno = S_ISDIR(info.st_mode) ? EISDIR : EINVAL;
        return -1;
    }
    if (info.st_size < 0) {
        errno = EOVERFLOW;
        return -1;
    }
    *size = (uint64_t)info.st_size;
    return 0;
}

int photoc_fs_filename(const char *path, char **filename)
{
    if (filename == NULL) {
        errno = EINVAL;
        return -1;
    }
    *filename = NULL;

    size_t start;
    size_t length;
    if (basename_bounds(path, &start, &length) != 0) {
        return -1;
    }
    return copy_part(path + start, length, filename);
}

int photoc_fs_compare_name_then_path(const char *left, const char *right)
{
    const char *left_slash = strrchr(left, '/');
    const char *right_slash = strrchr(right, '/');
    const char *left_name = left_slash == NULL ? left : left_slash + 1;
    const char *right_name = right_slash == NULL ? right : right_slash + 1;
    int order = strcmp(left_name, right_name);
    return order != 0 ? order : strcmp(left, right);
}

int photoc_fs_extension(const char *path, char **extension)
{
    if (extension == NULL) {
        errno = EINVAL;
        return -1;
    }
    *extension = NULL;

    size_t start;
    size_t length;
    int result = extension_bounds(path, &start, &length);
    if (result <= 0) {
        return result;
    }
    return copy_part(path + start, length, extension);
}

static char ascii_lower(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

const char *photoc_fs_relative(const char *root, const char *path)
{
    if (root == NULL || path == NULL) {
        return path;
    }

    size_t root_length = strlen(root);
    while (root_length > 1 && root[root_length - 1] == '/') {
        --root_length;
    }
    if (root_length == 0) {
        return path;
    }

    size_t path_length = strlen(path);
    if (path_length < root_length || memcmp(path, root, root_length) != 0) {
        return path;
    }
    /* "/" is a prefix of every absolute path. Any other root must end at a
       directory boundary so "/photos" does not match "/photos2". */
    bool root_is_slash = root_length == 1 && root[0] == '/';
    if (path_length > root_length && path[root_length] != '/' &&
        !root_is_slash) {
        return path;
    }

    const char *relative = path + root_length;
    while (*relative == '/') {
        ++relative;
    }
    return relative;
}

bool photoc_fs_is_jpeg(const char *path)
{
    size_t start;
    size_t length;
    if (extension_bounds(path, &start, &length) != 1) {
        return false;
    }

    if (length == 3) {
        return ascii_lower(path[start]) == 'j' &&
               ascii_lower(path[start + 1]) == 'p' &&
               ascii_lower(path[start + 2]) == 'g';
    }
    if (length == 4) {
        return ascii_lower(path[start]) == 'j' &&
               ascii_lower(path[start + 1]) == 'p' &&
               ascii_lower(path[start + 2]) == 'e' &&
               ascii_lower(path[start + 3]) == 'g';
    }
    return false;
}

bool photoc_fs_is_arw(const char *path)
{
    size_t start;
    size_t length;
    return extension_bounds(path, &start, &length) == 1 && length == 3 &&
           ascii_lower(path[start]) == 'a' &&
           ascii_lower(path[start + 1]) == 'r' &&
           ascii_lower(path[start + 2]) == 'w';
}

int photoc_fs_compare_casefold(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        unsigned char a = (unsigned char)ascii_lower(*left++);
        unsigned char b = (unsigned char)ascii_lower(*right++);
        if (a != b) {
            return a < b ? -1 : 1;
        }
    }
    if (*left == *right) {
        return 0;
    }
    return *left == '\0' ? -1 : 1;
}

int photoc_fs_join(const char *base, const char *child, char **joined)
{
    if (joined == NULL) {
        errno = EINVAL;
        return -1;
    }
    *joined = NULL;
    if (!valid_path(base) || !valid_path(child) || child[0] == '/') {
        errno = EINVAL;
        return -1;
    }

    size_t base_length = strlen(base);
    size_t child_length = strlen(child);
    size_t separator_length = base[base_length - 1] == '/' ? 0 : 1;
    if (base_length > SIZE_MAX - separator_length - 1 ||
        child_length > SIZE_MAX - base_length - separator_length - 1) {
        errno = EOVERFLOW;
        return -1;
    }

    size_t length = base_length + separator_length + child_length;
    char *path = malloc(length + 1);
    if (path == NULL) {
        return -1;
    }
    memcpy(path, base, base_length);
    if (separator_length != 0) {
        path[base_length] = '/';
    }
    memcpy(path + base_length + separator_length, child, child_length);
    path[length] = '\0';
    *joined = path;
    return 0;
}

int photoc_fs_mkdirs(const char *path)
{
    if (!valid_path(path)) {
        return -1;
    }
    size_t length = strlen(path);
    char *copy = malloc(length + 1);
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, path, length + 1);

    int result = 0;
    int saved_errno = 0;
    for (size_t i = 1; i <= length; ++i) {
        if (copy[i] != '/' && copy[i] != '\0') {
            continue;
        }
        char delimiter = copy[i];
        copy[i] = '\0';
        struct stat info;
        if (lstat(copy, &info) != 0) {
            if (errno != ENOENT ||
                (mkdir(copy, 0777) != 0 && errno != EEXIST) ||
                lstat(copy, &info) != 0) {
                saved_errno = errno;
                result = -1;
            }
        }
        if (result == 0 && !S_ISDIR(info.st_mode)) {
            saved_errno = ENOTDIR;
            result = -1;
        }
        copy[i] = delimiter;
        if (result != 0) {
            break;
        }
    }
    free(copy);
    if (result != 0) {
        errno = saved_errno;
    }
    return result;
}

int photoc_fs_rename_noreplace(const char *source, const char *destination)
{
    if (!valid_path(source) || !valid_path(destination)) {
        errno = EINVAL;
        return -1;
    }
#if defined(__APPLE__)
    return renamex_np(source, destination, RENAME_EXCL);
#elif defined(__linux__)
    return renameat2(AT_FDCWD, source, AT_FDCWD, destination, RENAME_NOREPLACE);
#else
    errno = ENOTSUP;
    return -1;
#endif
}

int photoc_fs_renameat_noreplace(int source_directory, const char *source_name,
                                 int destination_directory,
                                 const char *destination_name)
{
    if (source_directory < 0 || destination_directory < 0 ||
        !valid_path(source_name) || !valid_path(destination_name) ||
        strchr(source_name, '/') != NULL ||
        strchr(destination_name, '/') != NULL) {
        errno = EINVAL;
        return -1;
    }
#if defined(__APPLE__)
    return renameatx_np(source_directory, source_name, destination_directory,
                        destination_name, RENAME_EXCL);
#elif defined(__linux__)
    return renameat2(source_directory, source_name, destination_directory,
                     destination_name, RENAME_NOREPLACE);
#else
    errno = ENOTSUP;
    return -1;
#endif
}

static int append_child(const char *directory, const char *name, char **buffer,
                        size_t *capacity)
{
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    size_t separator =
        directory_length != 0 && directory[directory_length - 1] != '/' ? 1 : 0;
    if (directory_length > SIZE_MAX - separator - 1 ||
        name_length > SIZE_MAX - directory_length - separator - 1) {
        errno = EOVERFLOW;
        return -1;
    }
    size_t needed = directory_length + separator + name_length + 1;
    if (*buffer == NULL || needed > *capacity) {
        char *grown = realloc(*buffer, needed);
        if (grown == NULL) {
            return -1;
        }
        *buffer = grown;
        *capacity = needed;
    }
    memcpy(*buffer, directory, directory_length);
    size_t at = directory_length;
    if (separator != 0) {
        (*buffer)[at++] = '/';
    }
    memcpy(*buffer + at, name, name_length + 1);
    return 0;
}

static int walk_directory(const char *directory, bool recursive,
                          photoc_fs_visit_fn visit, void *user_data)
{
    DIR *stream = opendir(directory);
    if (stream == NULL) {
        return -1;
    }

    int result = 0;
    int saved_errno = 0;
    char *child = NULL;
    size_t child_capacity = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(stream);
        if (entry == NULL) {
            if (errno != 0) {
                saved_errno = errno;
                result = -1;
            }
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (append_child(directory, entry->d_name, &child, &child_capacity) !=
            0) {
            saved_errno = errno;
            result = -1;
            break;
        }

        photoc_fs_type type = PHOTOC_FS_OTHER;
        bool have_type = true;
        switch (entry->d_type) {
        case DT_REG:
            type = PHOTOC_FS_FILE;
            break;
        case DT_DIR:
            type = PHOTOC_FS_DIRECTORY;
            break;
        case DT_LNK:
            type = PHOTOC_FS_OTHER;
            break;
        case DT_UNKNOWN:
            have_type = false;
            break;
        default:
            type = PHOTOC_FS_OTHER;
            break;
        }
        if (!have_type && photoc_fs_get_type(child, &type) != 0) {
            saved_errno = errno;
            result = -1;
        } else if (!visit(child, type, user_data)) {
            result = 1;
        } else if (recursive && type == PHOTOC_FS_DIRECTORY) {
            result = walk_directory(child, true, visit, user_data);
            if (result == -1) {
                saved_errno = errno;
            }
        }

        if (result != 0) {
            break;
        }
    }

    free(child);
    if (closedir(stream) != 0 && result != -1) {
        return -1;
    }
    if (result == -1) {
        errno = saved_errno;
    }
    return result;
}

static int start_walk(const char *directory, bool recursive,
                      photoc_fs_visit_fn visit, void *user_data)
{
    if (visit == NULL) {
        errno = EINVAL;
        return -1;
    }

    photoc_fs_type type;
    if (photoc_fs_get_type(directory, &type) != 0) {
        return -1;
    }
    if (type != PHOTOC_FS_DIRECTORY) {
        errno = ENOTDIR;
        return -1;
    }
    return walk_directory(directory, recursive, visit, user_data);
}

int photoc_fs_walk(const char *directory, photoc_fs_visit_fn visit,
                   void *user_data)
{
    return start_walk(directory, false, visit, user_data);
}

int photoc_fs_walk_recursive(const char *directory, photoc_fs_visit_fn visit,
                             void *user_data)
{
    return start_walk(directory, true, visit, user_data);
}
