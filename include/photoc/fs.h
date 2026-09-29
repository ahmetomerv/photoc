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
bool photoc_fs_is_arw(const char *path);

/* Returns the part of path after root. Trailing slashes on root are ignored,
   except that "/" is kept. The result is borrowed from path and has no
   leading slash. A NULL argument, a shorter path, or a path that does not
   start with root as a directory prefix returns path unchanged. */
const char *photoc_fs_relative(const char *root, const char *path);

/* Compares paths bytewise after folding ASCII A-Z. Useful for detecting
   portable destination-name collisions; does not inspect the filesystem. */
int photoc_fs_compare_casefold(const char *left, const char *right);

/* Lexical join of a nonempty base and a nonempty relative child.
   Absolute children are rejected. This does not enforce path containment.
   The output pointer must not own memory on entry; *joined must be freed by
   the caller. */
int photoc_fs_join(const char *base, const char *child, char **joined);

/* Create a directory and missing parents, refusing symlink components.
   Existing directories are accepted. Returns 0 or -1 with errno set. */
int photoc_fs_mkdirs(const char *path);

/* Atomically renames a path only when destination does not exist. Never
   falls back to ordinary rename(), which could overwrite a file. Returns
   -1 with errno (including EEXIST or an unsupported-filesystem error) when
   the operation cannot be completed. Both paths must be nonempty. */
int photoc_fs_rename_noreplace(const char *source, const char *destination);

/* Descriptor-relative variant. Source and destination names must be nonempty
   single path components; the caller owns the directory descriptors. */
int photoc_fs_renameat_noreplace(int source_directory, const char *source_name,
                                 int destination_directory,
                                 const char *destination_name);

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
