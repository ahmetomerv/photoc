#ifndef PHOTOC_FS_H
#define PHOTOC_FS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PHOTOC_FS_FILE,
    PHOTOC_FS_DIRECTORY,
    PHOTOC_FS_OTHER
} photoc_fs_type;

/* These functions return 0 on success or -1 with errno set on failure.
   Path inspection uses lstat: symbolic links exist but have type OTHER.
   File size is available only for regular files; directories fail with EISDIR. */
int photoc_fs_exists(const char *path, bool *exists);
int photoc_fs_get_type(const char *path, photoc_fs_type *type);
int photoc_fs_file_size(const char *path, uint64_t *size);

/* Output pointers must not own memory on entry. Strings are allocated with
   malloc and must be freed by the caller.
   Trailing slashes are ignored; the filename of "/" is "/".
   An extension has no leading dot. No extension returns 0 with *extension NULL. */
int photoc_fs_filename(const char *path, char **filename);
int photoc_fs_extension(const char *path, char **extension);
bool photoc_fs_is_jpeg(const char *path);

/* Lexical join of a nonempty base and a nonempty relative child.
   Absolute children are rejected. This does not enforce path containment.
   The output pointer must not own memory on entry; *joined must be freed by
   the caller. */
int photoc_fs_join(const char *base, const char *child, char **joined);

/* The callback receives each child (not the root) in unspecified order.
   Its path is valid only during the callback; copy it to retain it.
   Returning false stops the walk. Walks return 0 when complete, 1 when
   stopped by the callback, or -1 with errno set on a filesystem error.
   Symlinks are reported as OTHER and are never followed. */
typedef bool (*photoc_fs_visit_fn)(const char *path, photoc_fs_type type,
                                   void *user_data);

int photoc_fs_walk(const char *directory, photoc_fs_visit_fn visit,
                   void *user_data);
int photoc_fs_walk_recursive(const char *directory, photoc_fs_visit_fn visit,
                             void *user_data);

#endif
