#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/fs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s (errno=%d)\n", __FILE__, __LINE__,      \
                    #condition, errno);                                        \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

typedef struct {
    char *root;
    char *photo;
    char *mixed_case;
    char *notes;
    char *subdir;
    char *nested_photo;
    char *deep_dir;
    char *deep_photo;
    char *empty_dir;
    char *link_dir;
    char *dangling_link;
    char *missing;
} test_tree;

static void destroy_tree(test_tree *tree)
{
    if (tree->dangling_link != NULL)
        unlink(tree->dangling_link);
    if (tree->link_dir != NULL)
        unlink(tree->link_dir);
    if (tree->deep_photo != NULL)
        unlink(tree->deep_photo);
    if (tree->nested_photo != NULL)
        unlink(tree->nested_photo);
    if (tree->notes != NULL)
        unlink(tree->notes);
    if (tree->mixed_case != NULL)
        unlink(tree->mixed_case);
    if (tree->photo != NULL)
        unlink(tree->photo);
    if (tree->deep_dir != NULL)
        rmdir(tree->deep_dir);
    if (tree->empty_dir != NULL)
        rmdir(tree->empty_dir);
    if (tree->subdir != NULL)
        rmdir(tree->subdir);
    if (tree->root != NULL)
        rmdir(tree->root);

    free(tree->missing);
    free(tree->dangling_link);
    free(tree->link_dir);
    free(tree->empty_dir);
    free(tree->deep_photo);
    free(tree->deep_dir);
    free(tree->nested_photo);
    free(tree->subdir);
    free(tree->notes);
    free(tree->mixed_case);
    free(tree->photo);
    free(tree->root);
}

static int write_file(const char *path, const char *contents)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }
    size_t length = strlen(contents);
    int result = fwrite(contents, 1, length, file) == length ? 0 : -1;
    if (fclose(file) != 0) {
        result = -1;
    }
    return result;
}

static int create_tree(test_tree *tree)
{
    const char *temporary_directory = getenv("TMPDIR");
    if (temporary_directory == NULL || temporary_directory[0] == '\0') {
        temporary_directory = "/tmp";
    }
    if (photoc_fs_join(temporary_directory, "photoc-fs-XXXXXX", &tree->root) !=
            0 ||
        mkdtemp(tree->root) == NULL ||
        photoc_fs_join(tree->root, "photo.jpg", &tree->photo) != 0 ||
        photoc_fs_join(tree->root, "mixed.JpEg", &tree->mixed_case) != 0 ||
        photoc_fs_join(tree->root, "notes.txt", &tree->notes) != 0 ||
        photoc_fs_join(tree->root, "subdir", &tree->subdir) != 0 ||
        photoc_fs_join(tree->subdir, "inside.jpeg", &tree->nested_photo) != 0 ||
        photoc_fs_join(tree->subdir, "deep", &tree->deep_dir) != 0 ||
        photoc_fs_join(tree->deep_dir, "deep.JPG", &tree->deep_photo) != 0 ||
        photoc_fs_join(tree->root, "empty", &tree->empty_dir) != 0 ||
        photoc_fs_join(tree->root, "linkdir", &tree->link_dir) != 0 ||
        photoc_fs_join(tree->root, "dangling", &tree->dangling_link) != 0 ||
        photoc_fs_join(tree->root, "missing", &tree->missing) != 0 ||
        mkdir(tree->subdir, 0700) != 0 || mkdir(tree->deep_dir, 0700) != 0 ||
        mkdir(tree->empty_dir, 0700) != 0 ||
        write_file(tree->photo, "abc") != 0 ||
        write_file(tree->mixed_case, "") != 0 ||
        write_file(tree->notes, "note") != 0 ||
        write_file(tree->nested_photo, "nested") != 0 ||
        write_file(tree->deep_photo, "deep") != 0 ||
        symlink("subdir", tree->link_dir) != 0 ||
        symlink("missing", tree->dangling_link) != 0) {
        perror("creating filesystem test tree");
        return -1;
    }
    return 0;
}

static void expect_filename(const char *path, const char *expected)
{
    char *actual = NULL;
    int result = photoc_fs_filename(path, &actual);
    CHECK(result == 0);
    if (result == 0) {
        CHECK(actual != NULL && strcmp(actual, expected) == 0);
    }
    free(actual);
}

static void expect_extension(const char *path, const char *expected)
{
    char *actual = NULL;
    int result = photoc_fs_extension(path, &actual);
    CHECK(result == 0);
    if (result == 0) {
        if (expected == NULL) {
            CHECK(actual == NULL);
        } else {
            CHECK(actual != NULL && strcmp(actual, expected) == 0);
        }
    }
    free(actual);
}

static void expect_join(const char *base, const char *child,
                        const char *expected)
{
    char *actual = NULL;
    int result = photoc_fs_join(base, child, &actual);
    CHECK(result == 0);
    if (result == 0) {
        CHECK(actual != NULL && strcmp(actual, expected) == 0);
    }
    free(actual);
}

static void test_path_strings(void)
{
    expect_filename("/photos/photo.JPG", "photo.JPG");
    expect_filename("/photos/photo.JPG///", "photo.JPG");
    expect_filename("/", "/");
    expect_filename("////", "/");
    expect_filename("photo.jpg", "photo.jpg");

    expect_extension("/photos/photo.JPG", "JPG");
    expect_extension("/photos/.hidden.jpeg", "jpeg");
    expect_extension("archive.tar.gz", "gz");
    expect_extension(".hidden", NULL);
    expect_extension("photo.", NULL);
    expect_extension("/photos.jpg/photo", NULL);
    expect_extension("/", NULL);

    CHECK(photoc_fs_is_jpeg("photo.jpg"));
    CHECK(photoc_fs_is_jpeg("photo.JpEg"));
    CHECK(photoc_fs_is_jpeg("/photos/.hidden.JPEG"));
    CHECK(!photoc_fs_is_jpeg("photo.jpgx"));
    CHECK(!photoc_fs_is_jpeg("/photos.jpg/photo"));
    CHECK(!photoc_fs_is_jpeg(".jpg"));
    CHECK(!photoc_fs_is_jpeg(NULL));

    CHECK(strcmp(photoc_fs_relative("/photos/", "/photos/a.jpg"), "a.jpg") ==
          0);
    CHECK(strcmp(photoc_fs_relative("/photos", "/photos/nested/a.jpg"),
                 "nested/a.jpg") == 0);
    CHECK(strcmp(photoc_fs_relative("/", "/a.jpg"), "a.jpg") == 0);
    CHECK(strcmp(photoc_fs_relative("/photos///", "/photos///a.jpg"),
                 "a.jpg") == 0);
    CHECK(strcmp(photoc_fs_relative("/photos", "/photos"), "") == 0);
    CHECK(strcmp(photoc_fs_relative("/photos/nested", "/photos"), "/photos") ==
          0);
    CHECK(strcmp(photoc_fs_relative("/photos", "/photos2/a.jpg"),
                 "/photos2/a.jpg") == 0);
    CHECK(strcmp(photoc_fs_relative(NULL, "/photos/a.jpg"), "/photos/a.jpg") ==
          0);
    CHECK(photoc_fs_relative("/photos", NULL) == NULL);

    expect_join("/photos", "photo.jpg", "/photos/photo.jpg");
    expect_join("/photos/", "photo.jpg", "/photos/photo.jpg");
    expect_join("/", "photo.jpg", "/photo.jpg");
    expect_join("photos", "nested/photo.jpg", "photos/nested/photo.jpg");

    char *long_base = malloc(8193);
    CHECK(long_base != NULL);
    if (long_base != NULL) {
        memset(long_base, 'a', 8192);
        long_base[8192] = '\0';
        char *long_join = NULL;
        CHECK(photoc_fs_join(long_base, "photo.jpg", &long_join) == 0);
        if (long_join != NULL) {
            CHECK(strlen(long_join) == 8202);
            CHECK(strcmp(long_join + 8192, "/photo.jpg") == 0);
        }
        free(long_join);
        free(long_base);
    }

    char *result = NULL;
    errno = 0;
    CHECK(photoc_fs_join("/photos", "/absolute", &result) == -1 &&
          errno == EINVAL);
    CHECK(result == NULL);
    errno = 0;
    CHECK(photoc_fs_join("", "photo.jpg", &result) == -1 && errno == EINVAL);
    CHECK(result == NULL);
    errno = 0;
    CHECK(photoc_fs_filename("", &result) == -1 && errno == EINVAL);
    CHECK(result == NULL);
    errno = 0;
    CHECK(photoc_fs_extension(NULL, &result) == -1 && errno == EINVAL);
    CHECK(result == NULL);
}

static void test_stat_paths(const test_tree *tree)
{
    bool exists = false;
    CHECK(photoc_fs_exists(tree->photo, &exists) == 0 && exists);
    CHECK(photoc_fs_exists(tree->subdir, &exists) == 0 && exists);
    CHECK(photoc_fs_exists(tree->dangling_link, &exists) == 0 && exists);
    CHECK(photoc_fs_exists(tree->missing, &exists) == 0 && !exists);

    photoc_fs_type type;
    CHECK(photoc_fs_get_type(tree->photo, &type) == 0 &&
          type == PHOTOC_FS_FILE);
    CHECK(photoc_fs_get_type(tree->subdir, &type) == 0 &&
          type == PHOTOC_FS_DIRECTORY);
    CHECK(photoc_fs_get_type(tree->link_dir, &type) == 0 &&
          type == PHOTOC_FS_OTHER);
    errno = 0;
    CHECK(photoc_fs_get_type(tree->missing, &type) == -1 && errno == ENOENT);

    uint64_t size = UINT64_MAX;
    CHECK(photoc_fs_file_size(tree->photo, &size) == 0 && size == 3);
    CHECK(photoc_fs_file_size(tree->mixed_case, &size) == 0 && size == 0);
    CHECK(photoc_fs_file_size(PHOTOC_TEST_FIXTURE, &size) == 0 && size == 15);
    errno = 0;
    CHECK(photoc_fs_file_size(tree->subdir, &size) == -1 && errno == EISDIR);
    errno = 0;
    CHECK(photoc_fs_file_size(tree->link_dir, &size) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(photoc_fs_file_size(tree->missing, &size) == -1 && errno == ENOENT);

    errno = 0;
    CHECK(photoc_fs_exists(NULL, &exists) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(photoc_fs_get_type(tree->photo, NULL) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(photoc_fs_file_size(tree->photo, NULL) == -1 && errno == EINVAL);
}

typedef struct {
    const char *paths[10];
    photoc_fs_type types[10];
    bool seen[10];
    size_t expected_count;
    size_t seen_count;
    bool unexpected;
} walk_result;

static bool record_visit(const char *path, photoc_fs_type type, void *user_data)
{
    walk_result *result = user_data;
    for (size_t i = 0; i < result->expected_count; ++i) {
        if (strcmp(path, result->paths[i]) == 0) {
            if (result->seen[i] || result->types[i] != type) {
                result->unexpected = true;
            }
            result->seen[i] = true;
            ++result->seen_count;
            return true;
        }
    }
    result->unexpected = true;
    return true;
}

static bool stop_visit(const char *path, photoc_fs_type type, void *user_data)
{
    (void)path;
    (void)type;
    size_t *count = user_data;
    ++*count;
    return false;
}

static void check_walk_result(const walk_result *result)
{
    CHECK(!result->unexpected);
    CHECK(result->seen_count == result->expected_count);
    for (size_t i = 0; i < result->expected_count; ++i) {
        CHECK(result->seen[i]);
    }
}

static void test_walks(const test_tree *tree)
{
    walk_result shallow = {
        .paths = {tree->photo, tree->mixed_case, tree->notes, tree->subdir,
                  tree->empty_dir, tree->link_dir, tree->dangling_link},
        .types = {PHOTOC_FS_FILE, PHOTOC_FS_FILE, PHOTOC_FS_FILE,
                  PHOTOC_FS_DIRECTORY, PHOTOC_FS_DIRECTORY, PHOTOC_FS_OTHER,
                  PHOTOC_FS_OTHER},
        .expected_count = 7};
    CHECK(photoc_fs_walk(tree->root, record_visit, &shallow) == 0);
    check_walk_result(&shallow);

    walk_result deep = shallow;
    memset(deep.seen, 0, sizeof(deep.seen));
    deep.seen_count = 0;
    deep.paths[7] = tree->nested_photo;
    deep.paths[8] = tree->deep_dir;
    deep.paths[9] = tree->deep_photo;
    deep.types[7] = PHOTOC_FS_FILE;
    deep.types[8] = PHOTOC_FS_DIRECTORY;
    deep.types[9] = PHOTOC_FS_FILE;
    deep.expected_count = 10;
    CHECK(photoc_fs_walk_recursive(tree->root, record_visit, &deep) == 0);
    check_walk_result(&deep);

    walk_result empty = {0};
    CHECK(photoc_fs_walk(tree->empty_dir, record_visit, &empty) == 0);
    CHECK(empty.seen_count == 0 && !empty.unexpected);

    size_t stopped_count = 0;
    CHECK(photoc_fs_walk_recursive(tree->root, stop_visit, &stopped_count) ==
          1);
    CHECK(stopped_count == 1);

    errno = 0;
    CHECK(photoc_fs_walk(tree->photo, record_visit, &empty) == -1 &&
          errno == ENOTDIR);
    errno = 0;
    CHECK(photoc_fs_walk_recursive(tree->link_dir, record_visit, &empty) ==
              -1 &&
          errno == ENOTDIR);
    errno = 0;
    CHECK(photoc_fs_walk(tree->missing, record_visit, &empty) == -1 &&
          errno == ENOENT);
    errno = 0;
    CHECK(photoc_fs_walk(tree->root, NULL, &empty) == -1 && errno == EINVAL);
}

static void test_rename_noreplace(const test_tree *tree)
{
    char *source = NULL;
    char *existing = NULL;
    char *destination = NULL;
    if (photoc_fs_join(tree->root, "rename-source.jpg", &source) != 0 ||
        photoc_fs_join(tree->root, "rename-existing.jpg", &existing) != 0 ||
        photoc_fs_join(tree->root, "rename-destination.jpg", &destination) !=
            0 ||
        write_file(source, "source-data") != 0 ||
        write_file(existing, "existing-data") != 0) {
        CHECK(false);
        goto done;
    }

    errno = 0;
    CHECK(photoc_fs_rename_noreplace(source, existing) == -1 &&
          errno == EEXIST);
    uint64_t size = 0;
    CHECK(photoc_fs_file_size(source, &size) == 0 && size == 11);
    CHECK(photoc_fs_file_size(existing, &size) == 0 && size == 13);
    CHECK(photoc_fs_rename_noreplace(source, destination) == 0);
    bool exists = true;
    CHECK(photoc_fs_exists(source, &exists) == 0 && !exists);
    CHECK(photoc_fs_file_size(destination, &size) == 0 && size == 11);
    CHECK(photoc_fs_file_size(existing, &size) == 0 && size == 13);

    errno = 0;
    CHECK(photoc_fs_rename_noreplace(NULL, destination) == -1 &&
          errno == EINVAL);

done:
    if (source != NULL)
        unlink(source);
    if (existing != NULL)
        unlink(existing);
    if (destination != NULL)
        unlink(destination);
    free(source);
    free(existing);
    free(destination);
}

static void test_renameat_noreplace(const test_tree *tree)
{
    char *source = NULL;
    char *existing = NULL;
    char *destination = NULL;
    int directory_fd = open(tree->root, O_RDONLY);
    if (directory_fd < 0 ||
        photoc_fs_join(tree->root, "renameat-source.jpg", &source) != 0 ||
        photoc_fs_join(tree->root, "renameat-existing.jpg", &existing) != 0 ||
        photoc_fs_join(tree->root, "renameat-destination.jpg", &destination) !=
            0 ||
        write_file(source, "source-data") != 0 ||
        write_file(existing, "existing-data") != 0) {
        CHECK(false);
        goto done;
    }
    errno = 0;
    CHECK(photoc_fs_renameat_noreplace(directory_fd, "renameat-source.jpg",
                                       directory_fd,
                                       "renameat-existing.jpg") == -1 &&
          errno == EEXIST);
    uint64_t size = 0;
    CHECK(photoc_fs_file_size(source, &size) == 0 && size == 11);
    CHECK(photoc_fs_file_size(existing, &size) == 0 && size == 13);
    CHECK(photoc_fs_renameat_noreplace(directory_fd, "renameat-source.jpg",
                                       directory_fd,
                                       "renameat-destination.jpg") == 0);
    CHECK(photoc_fs_file_size(destination, &size) == 0 && size == 11);
    errno = 0;
    CHECK(photoc_fs_renameat_noreplace(directory_fd, "../invalid", directory_fd,
                                       "another.jpg") == -1 &&
          errno == EINVAL);

done:
    if (directory_fd >= 0)
        close(directory_fd);
    if (source != NULL)
        unlink(source);
    if (existing != NULL)
        unlink(existing);
    if (destination != NULL)
        unlink(destination);
    free(source);
    free(existing);
    free(destination);
}

int main(void)
{
    test_path_strings();

    test_tree tree = {0};
    if (create_tree(&tree) == 0) {
        test_stat_paths(&tree);
        test_walks(&tree);
        test_rename_noreplace(&tree);
        test_renameat_noreplace(&tree);
    } else {
        ++failures;
    }
    destroy_tree(&tree);

    if (failures != 0) {
        fprintf(stderr, "%d filesystem test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
