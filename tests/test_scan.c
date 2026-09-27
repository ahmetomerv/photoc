#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/fs.h"
#include "photoc/scan.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: %s (errno=%d)\n", __FILE__, __LINE__,     \
                    #condition, errno);                                        \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

typedef struct {
    char *root;
    char *camera;
    char *bare;
    char *broken;
    char *notes;
    char *graphic;
    char *nested;
    char *gps;
    char *deep;
    char *deep_photo;
    char *empty;
    char *link;
    char *missing;
} test_tree;

static void destroy_tree(test_tree *tree)
{
    if (tree->link != NULL) unlink(tree->link);
    if (tree->deep_photo != NULL) unlink(tree->deep_photo);
    if (tree->gps != NULL) unlink(tree->gps);
    if (tree->graphic != NULL) unlink(tree->graphic);
    if (tree->notes != NULL) unlink(tree->notes);
    if (tree->broken != NULL) unlink(tree->broken);
    if (tree->bare != NULL) unlink(tree->bare);
    if (tree->camera != NULL) unlink(tree->camera);
    if (tree->deep != NULL) rmdir(tree->deep);
    if (tree->nested != NULL) rmdir(tree->nested);
    if (tree->empty != NULL) rmdir(tree->empty);
    if (tree->root != NULL) rmdir(tree->root);

    free(tree->missing);
    free(tree->link);
    free(tree->empty);
    free(tree->deep_photo);
    free(tree->deep);
    free(tree->gps);
    free(tree->nested);
    free(tree->graphic);
    free(tree->notes);
    free(tree->broken);
    free(tree->bare);
    free(tree->camera);
    free(tree->root);
}

static int copy_fixture(const char *name, const char *destination)
{
    char *source_path = NULL;
    if (photoc_fs_join(PHOTOC_SCAN_FIXTURES, name, &source_path) != 0) {
        return -1;
    }
    FILE *source = fopen(source_path, "rb");
    free(source_path);
    if (source == NULL) {
        return -1;
    }
    FILE *target = fopen(destination, "wb");
    if (target == NULL) {
        fclose(source);
        return -1;
    }

    unsigned char buffer[4096];
    size_t count;
    int result = 0;
    while ((count = fread(buffer, 1, sizeof(buffer), source)) != 0) {
        if (fwrite(buffer, 1, count, target) != count) {
            result = -1;
            break;
        }
    }
    if (ferror(source)) {
        result = -1;
    }
    if (fclose(source) != 0) {
        result = -1;
    }
    if (fclose(target) != 0) {
        result = -1;
    }
    return result;
}

static int write_notes(const char *path)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }
    int result = fputs("not a photo\n", file) == EOF ? -1 : 0;
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
    if (photoc_fs_join(temporary_directory, "photoc-scan-XXXXXX", &tree->root) != 0 ||
        mkdtemp(tree->root) == NULL ||
        photoc_fs_join(tree->root, "camera.JPG", &tree->camera) != 0 ||
        photoc_fs_join(tree->root, "bare.jpeg", &tree->bare) != 0 ||
        photoc_fs_join(tree->root, "broken.jpg", &tree->broken) != 0 ||
        photoc_fs_join(tree->root, "notes.txt", &tree->notes) != 0 ||
        photoc_fs_join(tree->root, "graphic.png", &tree->graphic) != 0 ||
        photoc_fs_join(tree->root, "nested", &tree->nested) != 0 ||
        photoc_fs_join(tree->nested, "gps.jpeg", &tree->gps) != 0 ||
        photoc_fs_join(tree->nested, "deep", &tree->deep) != 0 ||
        photoc_fs_join(tree->deep, "deep.jpg", &tree->deep_photo) != 0 ||
        photoc_fs_join(tree->root, "empty", &tree->empty) != 0 ||
        photoc_fs_join(tree->root, "link.jpg", &tree->link) != 0 ||
        photoc_fs_join(tree->root, "missing", &tree->missing) != 0 ||
        mkdir(tree->nested, 0700) != 0 ||
        mkdir(tree->deep, 0700) != 0 ||
        mkdir(tree->empty, 0700) != 0 ||
        copy_fixture("with_exif.jpg", tree->camera) != 0 ||
        copy_fixture("no_exif.jpg", tree->bare) != 0 ||
        copy_fixture("invalid.jpg", tree->broken) != 0 ||
        copy_fixture("unsupported.png", tree->graphic) != 0 ||
        copy_fixture("with_gps.jpeg", tree->gps) != 0 ||
        copy_fixture("no_exif.jpg", tree->deep_photo) != 0 ||
        write_notes(tree->notes) != 0 ||
        symlink("camera.JPG", tree->link) != 0) {
        perror("creating scanner test tree");
        return -1;
    }
    return 0;
}

typedef struct {
    unsigned int photos;
    unsigned int warnings;
    bool camera_seen;
    bool bare_seen;
    bool gps_seen;
    bool deep_seen;
    bool wrong_warning;
    bool stop_after_first;
} observations;

static bool observe_photo(const Photo *photo, void *user_data)
{
    observations *seen = user_data;
    ++seen->photos;
    CHECK(photo->path != NULL);
    CHECK(photo->has_file_size && photo->has_width && photo->has_height);
    CHECK(photo->width == 3 && photo->height == 2);

    const char *name = strrchr(photo->path, '/');
    name = name == NULL ? photo->path : name + 1;
    if (strcmp(name, "camera.JPG") == 0) {
        seen->camera_seen = true;
        CHECK(photo->camera_make != NULL && photo->has_iso);
    } else if (strcmp(name, "bare.jpeg") == 0) {
        seen->bare_seen = true;
        CHECK(photo->camera_make == NULL && !photo->has_gps);
    } else if (strcmp(name, "gps.jpeg") == 0) {
        seen->gps_seen = true;
        CHECK(photo->has_gps);
    } else if (strcmp(name, "deep.jpg") == 0) {
        seen->deep_seen = true;
    } else {
        CHECK(false);
    }
    return !seen->stop_after_first;
}

static void observe_warning(const char *path, photoc_metadata_result reason,
                            int system_errno, void *user_data)
{
    observations *seen = user_data;
    ++seen->warnings;
    const char *name = strrchr(path, '/');
    name = name == NULL ? path : name + 1;
    if (strcmp(name, "broken.jpg") != 0 ||
        reason != PHOTOC_METADATA_INVALID_JPEG || system_errno != 0) {
        seen->wrong_warning = true;
    }
}

static void test_flat(const test_tree *tree)
{
    observations seen = {0};
    photoc_scan_stats stats = {0};
    CHECK(photoc_scan_directory(tree->root, false, observe_photo,
                                observe_warning, &seen, &stats) == 0);
    CHECK(stats.files_visited == 5);
    CHECK(stats.jpeg_files_found == 3);
    CHECK(stats.photos_parsed == 2);
    CHECK(stats.skipped_files == 3);
    CHECK(stats.errors == 1);
    CHECK(seen.photos == 2 && seen.camera_seen && seen.bare_seen);
    CHECK(!seen.gps_seen && !seen.deep_seen);
    CHECK(seen.warnings == 1 && !seen.wrong_warning);
}

static void test_recursive(const test_tree *tree)
{
    observations seen = {0};
    photoc_scan_stats stats = {0};
    CHECK(photoc_scan_directory(tree->root, true, observe_photo,
                                observe_warning, &seen, &stats) == 0);
    CHECK(stats.files_visited == 7);
    CHECK(stats.jpeg_files_found == 5);
    CHECK(stats.photos_parsed == 4);
    CHECK(stats.skipped_files == 3);
    CHECK(stats.errors == 1);
    CHECK(seen.photos == 4 && seen.camera_seen && seen.bare_seen &&
          seen.gps_seen && seen.deep_seen);
    CHECK(seen.warnings == 1 && !seen.wrong_warning);
}

static void test_empty_and_stop(const test_tree *tree)
{
    observations seen = {0};
    photoc_scan_stats stats = {0};
    CHECK(photoc_scan_directory(tree->empty, true, observe_photo,
                                observe_warning, &seen, &stats) == 0);
    CHECK(stats.files_visited == 0 && stats.jpeg_files_found == 0 &&
          stats.photos_parsed == 0 && stats.skipped_files == 0 &&
          stats.errors == 0);
    CHECK(seen.photos == 0 && seen.warnings == 0);

    seen = (observations){.stop_after_first = true};
    CHECK(photoc_scan_directory(tree->root, true, observe_photo,
                                observe_warning, &seen, &stats) == 1);
    CHECK(stats.photos_parsed == 1 && seen.photos == 1);
}

typedef struct {
    unsigned int warnings;
    int expected_errno;
    bool wrong_warning;
} io_observations;

static bool unexpected_photo(const Photo *photo, void *user_data)
{
    (void)photo;
    (void)user_data;
    CHECK(false);
    return true;
}

static void observe_io_warning(const char *path, photoc_metadata_result reason,
                               int system_errno, void *user_data)
{
    io_observations *seen = user_data;
    ++seen->warnings;
    const char *name = strrchr(path, '/');
    name = name == NULL ? path : name + 1;
    if (strcmp(name, "unreadable.jpg") != 0 ||
        reason != PHOTOC_METADATA_IO_ERROR ||
        system_errno != seen->expected_errno) {
        seen->wrong_warning = true;
    }
}

static void test_unreadable_file(const test_tree *tree)
{
    char *path = NULL;
    CHECK(photoc_fs_join(tree->empty, "unreadable.jpg", &path) == 0);
    if (path == NULL) {
        return;
    }
    CHECK(copy_fixture("with_exif.jpg", path) == 0);
    CHECK(chmod(path, 0000) == 0);

    FILE *probe = fopen(path, "rb");
    if (probe == NULL) {
        io_observations seen = {.expected_errno = errno};
        photoc_scan_stats stats = {0};
        CHECK(photoc_scan_directory(tree->empty, false, unexpected_photo,
                                    observe_io_warning, &seen, &stats) == 0);
        CHECK(stats.files_visited == 1 && stats.jpeg_files_found == 1);
        CHECK(stats.photos_parsed == 0 && stats.skipped_files == 1 &&
              stats.errors == 1);
        CHECK(seen.warnings == 1 && !seen.wrong_warning);
    } else {
        fclose(probe); /* Elevated users may still be able to read mode 0000. */
    }

    CHECK(chmod(path, 0600) == 0);
    CHECK(unlink(path) == 0);
    free(path);
}

static void test_errors(const test_tree *tree)
{
    observations seen = {0};
    photoc_scan_stats stats = {0};
    errno = 0;
    CHECK(photoc_scan_directory(tree->missing, false, observe_photo,
                                observe_warning, &seen, &stats) == -1 &&
          errno == ENOENT);
    CHECK(stats.files_visited == 0 && stats.errors == 0);
    errno = 0;
    CHECK(photoc_scan_directory(tree->camera, false, observe_photo,
                                observe_warning, &seen, &stats) == -1 &&
          errno == ENOTDIR);
    errno = 0;
    CHECK(photoc_scan_directory(tree->root, false, NULL,
                                observe_warning, &seen, &stats) == -1 &&
          errno == EINVAL);
    CHECK(photoc_scan_directory(tree->root, false, observe_photo,
                                NULL, &seen, &stats) == 0);
    CHECK(stats.errors == 1 && seen.warnings == 0);
}

int main(void)
{
    test_tree tree = {0};
    if (create_tree(&tree) == 0) {
        test_flat(&tree);
        test_recursive(&tree);
        test_empty_and_stop(&tree);
        test_unreadable_file(&tree);
        test_errors(&tree);
    } else {
        ++failures;
    }
    destroy_tree(&tree);
    if (failures != 0) {
        fprintf(stderr, "%d scanner test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
