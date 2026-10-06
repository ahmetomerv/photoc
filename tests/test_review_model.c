#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "review_model.h"

#include "photoc/fs.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s (errno=%d)\n", __FILE__, __LINE__,      \
                    #condition, errno);                                        \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static char *join(const char *base, const char *child)
{
    char *path = NULL;
    CHECK(photoc_fs_join(base, child, &path) == 0);
    return path;
}

static int write_bytes(const char *path, const char *bytes, size_t length)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return -1;
    int result = fwrite(bytes, 1, length, file) == length ? 0 : -1;
    if (fclose(file) != 0)
        result = -1;
    return result;
}

static int copy_fixture(const char *fixture, const char *destination)
{
    char *source_path = join(PHOTOC_REVIEW_FIXTURES, fixture);
    if (source_path == NULL)
        return -1;
    FILE *source = fopen(source_path, "rb");
    free(source_path);
    if (source == NULL)
        return -1;
    FILE *target = fopen(destination, "wb");
    if (target == NULL) {
        fclose(source);
        return -1;
    }
    char buffer[4096];
    int result = 0;
    for (;;) {
        size_t size = fread(buffer, 1, sizeof(buffer), source);
        if (size != 0 && fwrite(buffer, 1, size, target) != size) {
            result = -1;
            break;
        }
        if (size < sizeof(buffer)) {
            if (ferror(source))
                result = -1;
            break;
        }
    }
    if (fclose(target) != 0)
        result = -1;
    if (fclose(source) != 0)
        result = -1;
    return result;
}

static int add_fixture(const char *root, const char *name,
                       const char *fixture)
{
    char *path = join(root, name);
    if (path == NULL)
        return -1;
    int result = copy_fixture(fixture, path);
    free(path);
    return result;
}

static int add_bytes(const char *root, const char *name,
                     const char *bytes)
{
    char *path = join(root, name);
    if (path == NULL)
        return -1;
    int result = write_bytes(path, bytes, strlen(bytes));
    free(path);
    return result;
}

static int make_directory(const char *root, const char *name)
{
    char *path = join(root, name);
    if (path == NULL)
        return -1;
    int result = mkdir(path, 0700);
    free(path);
    return result;
}

static int remove_tree(const char *directory)
{
    DIR *stream = opendir(directory);
    if (stream == NULL)
        return -1;
    int result = 0;
    struct dirent *entry;
    while ((entry = readdir(stream)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        char *path = join(directory, entry->d_name);
        if (path == NULL) {
            result = -1;
            break;
        }
        struct stat info;
        if (lstat(path, &info) != 0 ||
            (S_ISDIR(info.st_mode) ? remove_tree(path) : unlink(path)) != 0)
            result = -1;
        free(path);
        if (result != 0)
            break;
    }
    if (closedir(stream) != 0)
        result = -1;
    if (result == 0 && rmdir(directory) != 0)
        result = -1;
    return result;
}

static void check_paths(const review_model *model, const char *const *expected,
                        size_t count)
{
    CHECK(model->count == count);
    if (model->count != count)
        return;
    for (size_t i = 0; i < count; ++i)
        CHECK(strcmp(model->items[i].relative_path, expected[i]) == 0);
}

static void test_discovery(const char *root)
{
    char *names = join(root, "names");
    CHECK(names != NULL);
    if (names == NULL)
        return;
    CHECK(mkdir(names, 0700) == 0);
    CHECK(add_fixture(names, "A.JPG", "flat.jpg") == 0);
    CHECK(add_fixture(names, "a.jpeg", "flat.jpg") == 0);
    CHECK(add_bytes(names, "b.jpg", "not a JPEG") == 0);
    CHECK(add_bytes(names, "note.txt", "text") == 0);
    CHECK(add_bytes(names, "other.png", "image") == 0);
    CHECK(make_directory(names, "nested") == 0);
    CHECK(add_fixture(names, "nested/A.JPG", "flat.jpg") == 0);
    CHECK(make_directory(names, "nested/deeper") == 0);
    CHECK(add_fixture(names, "nested/deeper/c.JpEg", "flat.jpg") == 0);
    char *link_file = join(names, "linked.jpg");
    char *link_dir = join(names, "linked_dir");
    CHECK(link_file != NULL && link_dir != NULL);
    if (link_file != NULL && link_dir != NULL) {
        CHECK(symlink("A.JPG", link_file) == 0);
        CHECK(symlink("nested", link_dir) == 0);
    }

    review_model model = {0};
    CHECK(review_model_load(&model, names, false, PHOTOC_REVIEW_SORT_NAME,
                            PHOTOC_REVIEW_SHOW_ALL) == 0);
    const char *flat[] = {"A.JPG", "a.jpeg", "b.jpg"};
    check_paths(&model, flat, 3);
    CHECK(model.visible_count == 3 && model.unmarked == 3);
    for (size_t i = 0; i < model.count; ++i)
        CHECK(model.items[i].capture_timestamp == NULL);
    review_model_cleanup(&model);

    CHECK(review_model_load(&model, names, true, PHOTOC_REVIEW_SORT_NAME,
                            PHOTOC_REVIEW_SHOW_ALL) == 0);
    const char *recursive[] = {"A.JPG", "nested/A.JPG", "a.jpeg", "b.jpg",
                               "nested/deeper/c.JpEg"};
    check_paths(&model, recursive, 5);
    CHECK(model.visible_count == 5 && model.unmarked == 5);
    char *removed = join(names, "nested/deeper/c.JpEg");
    CHECK(removed != NULL);
    if (removed != NULL) {
        CHECK(unlink(removed) == 0);
        CHECK(strcmp(model.items[4].relative_path,
                     "nested/deeper/c.JpEg") == 0);
        free(removed);
    }
    review_model_cleanup(&model);
    if (link_dir != NULL) {
        CHECK(review_model_load(&model, link_dir, false,
                                PHOTOC_REVIEW_SORT_NAME,
                                PHOTOC_REVIEW_SHOW_ALL) == -1);
        CHECK(model.items == NULL && model.count == 0);
    }
    free(link_file);
    free(link_dir);
    free(names);
}

static int invalidate_capture_month(const char *path)
{
    FILE *file = fopen(path, "r+b");
    if (file == NULL)
        return -1;
    unsigned char bytes[4096];
    size_t size = fread(bytes, 1, sizeof(bytes), file);
    int result = -1;
    const char date[] = "2026:09:27 12:34:56";
    for (size_t i = 0; i + sizeof(date) - 1 <= size; ++i) {
        if (memcmp(bytes + i, date, sizeof(date) - 1) == 0) {
            bytes[i + 5] = '1';
            bytes[i + 6] = '3';
            if (fseek(file, 0, SEEK_SET) == 0 &&
                fwrite(bytes, 1, size, file) == size)
                result = 0;
            break;
        }
    }
    if (fclose(file) != 0)
        result = -1;
    return result;
}

static void test_date_sort(const char *root)
{
    char *dates = join(root, "dates");
    CHECK(dates != NULL);
    if (dates == NULL)
        return;
    CHECK(mkdir(dates, 0700) == 0);
    CHECK(add_fixture(dates, "z-early.jpg", "with_alt_exif.jpg") == 0);
    CHECK(add_fixture(dates, "b-late.jpg", "with_exif.jpg") == 0);
    CHECK(add_fixture(dates, "a-late.jpg", "with_exif.jpg") == 0);
    CHECK(add_fixture(dates, "undated.jpg", "no_exif.jpg") == 0);
    CHECK(add_fixture(dates, "invalid-date.jpg", "with_exif.jpg") == 0);
    CHECK(add_bytes(dates, "broken.jpg", "invalid JPEG") == 0);
    CHECK(make_directory(dates, "a") == 0);
    CHECK(make_directory(dates, "z") == 0);
    CHECK(add_fixture(dates, "a/z.jpg", "with_exif.jpg") == 0);
    CHECK(add_fixture(dates, "z/a.jpg", "with_exif.jpg") == 0);
    char *invalid = join(dates, "invalid-date.jpg");
    CHECK(invalid != NULL);
    if (invalid != NULL) {
        CHECK(invalidate_capture_month(invalid) == 0);
        free(invalid);
    }

    review_model model = {0};
    CHECK(review_model_load(&model, dates, false, PHOTOC_REVIEW_SORT_DATE,
                            PHOTOC_REVIEW_SHOW_ALL) == 0);
    const char *expected[] = {"z-early.jpg", "a-late.jpg", "b-late.jpg",
                              "broken.jpg", "invalid-date.jpg", "undated.jpg"};
    check_paths(&model, expected, 6);
    CHECK(model.items[0].capture_timestamp != NULL);
    CHECK(model.items[1].capture_timestamp != NULL);
    CHECK(model.items[2].capture_timestamp != NULL);
    for (size_t i = 3; i < model.count; ++i)
        CHECK(model.items[i].capture_timestamp == NULL);
    review_model_cleanup(&model);

    CHECK(review_model_load(&model, dates, true, PHOTOC_REVIEW_SORT_DATE,
                            PHOTOC_REVIEW_SHOW_ALL) == 0);
    const char *recursive[] = {"z-early.jpg", "a-late.jpg", "a/z.jpg",
                               "b-late.jpg", "z/a.jpg", "broken.jpg",
                               "invalid-date.jpg", "undated.jpg"};
    check_paths(&model, recursive, 8);
    review_model_cleanup(&model);
    free(dates);
}

static void test_navigation_and_counts(const char *root)
{
    char *names = join(root, "names");
    CHECK(names != NULL);
    if (names == NULL)
        return;
    review_model model = {0};
    CHECK(review_model_load(&model, names, false, PHOTOC_REVIEW_SORT_NAME,
                            PHOTOC_REVIEW_SHOW_UNMARKED) == 0);
    CHECK(model.visible_count == 3 && model.unmarked == 3);
    CHECK(strcmp(review_model_current(&model)->relative_path, "A.JPG") == 0);
    CHECK(!review_model_previous(&model));
    CHECK(review_model_mark_current(&model, PHOTOC_REVIEW_PICKED) == 0);
    CHECK(model.picked == 1 && model.rejected == 0 && model.unmarked == 2);
    CHECK(model.visible_count == 2);
    CHECK(strcmp(review_model_current(&model)->relative_path, "a.jpeg") == 0);
    CHECK(review_model_mark_current(&model, PHOTOC_REVIEW_REJECTED) == 0);
    CHECK(model.visible_count == 1);
    CHECK(strcmp(review_model_current(&model)->relative_path, "b.jpg") == 0);
    CHECK(model.picked == 1 && model.rejected == 1 && model.unmarked == 1);
    CHECK(review_model_mark_current(&model, PHOTOC_REVIEW_PICKED) == 0);
    CHECK(model.visible_count == 0 && review_model_current(&model) == NULL);
    CHECK(model.picked == 2 && model.rejected == 1 && model.unmarked == 0);
    CHECK(review_model_mark_current(&model, PHOTOC_REVIEW_UNMARKED) == -1);
    CHECK(!review_model_next(&model) && !review_model_previous(&model));

    model.show = PHOTOC_REVIEW_SHOW_PICKED;
    CHECK(review_model_rebuild(&model) == 0);
    CHECK(model.visible_count == 2 && model.cursor == 0);
    CHECK(strcmp(review_model_current(&model)->relative_path, "A.JPG") == 0);
    CHECK(review_model_next(&model));
    CHECK(strcmp(review_model_current(&model)->relative_path, "b.jpg") == 0);
    CHECK(review_model_mark_current(&model, PHOTOC_REVIEW_REJECTED) == 0);
    CHECK(model.visible_count == 1 && model.cursor == 0);
    CHECK(strcmp(review_model_current(&model)->relative_path, "A.JPG") == 0);
    CHECK(review_model_mark_current(&model, PHOTOC_REVIEW_UNMARKED) == 0);
    CHECK(model.visible_count == 0 && model.unmarked == 1);

    model.show = PHOTOC_REVIEW_SHOW_REJECTED;
    CHECK(review_model_rebuild(&model) == 0);
    CHECK(model.visible_count == 2 && model.cursor == 0);
    CHECK(strcmp(review_model_current(&model)->relative_path, "a.jpeg") == 0);
    CHECK(review_model_mark_current(&model, PHOTOC_REVIEW_REJECTED) == 0);
    CHECK(strcmp(review_model_current(&model)->relative_path, "b.jpg") == 0);
    CHECK(review_model_mark_current(&model, PHOTOC_REVIEW_UNMARKED) == 0);
    CHECK(model.visible_count == 1 && model.cursor == 0);
    CHECK(strcmp(review_model_current(&model)->relative_path, "a.jpeg") == 0);

    model.show = PHOTOC_REVIEW_SHOW_ALL;
    CHECK(review_model_rebuild(&model) == 0);
    CHECK(model.visible_count == 3 && model.cursor == 0);
    CHECK(review_model_mark_current(&model, PHOTOC_REVIEW_UNMARKED) == 0);
    CHECK(model.cursor == 1);
    CHECK(review_model_previous(&model));
    CHECK(model.cursor == 0);
    CHECK(review_model_next(&model));
    CHECK(review_model_next(&model));
    CHECK(!review_model_next(&model));
    CHECK(review_model_previous(&model));
    CHECK(review_model_mark_current(&model, PHOTOC_REVIEW_PICKED) == 0);
    CHECK(model.cursor == 2 && model.picked == 1);
    review_model_cleanup(&model);
    free(names);
}

static void test_empty_growth_and_errors(const char *root)
{
    char *empty = join(root, "empty");
    char *growth = join(root, "growth");
    CHECK(empty != NULL && growth != NULL);
    if (empty == NULL || growth == NULL) {
        free(empty);
        free(growth);
        return;
    }
    CHECK(mkdir(empty, 0700) == 0);
    CHECK(mkdir(growth, 0700) == 0);
    review_model model = {0};
    CHECK(review_model_load(&model, empty, false, PHOTOC_REVIEW_SORT_NAME,
                            PHOTOC_REVIEW_SHOW_ALL) == 0);
    CHECK(model.count == 0 && model.visible_count == 0);
    review_model_cleanup(&model);
    for (size_t i = 0; i < 70; ++i) {
        char name[32];
        CHECK(snprintf(name, sizeof(name), "%03zu.jpg", i) > 0);
        CHECK(add_bytes(growth, name, "not decoded") == 0);
    }
    CHECK(review_model_load(&model, growth, false, PHOTOC_REVIEW_SORT_NAME,
                            PHOTOC_REVIEW_SHOW_ALL) == 0);
    CHECK(model.count == 70 && model.visible_count == 70);
    review_model_cleanup(&model);

    CHECK(review_model_load(&model, "missing", false, PHOTOC_REVIEW_SORT_NAME,
                            PHOTOC_REVIEW_SHOW_ALL) == -1);
    CHECK(model.items == NULL && model.count == 0);
    CHECK(review_model_load(&model, empty, false, (review_sort)99,
                            PHOTOC_REVIEW_SHOW_ALL) == -1);
    CHECK(review_model_load(&model, empty, false, PHOTOC_REVIEW_SORT_NAME,
                            (review_show)99) == -1);
    CHECK(review_model_load(NULL, empty, false, PHOTOC_REVIEW_SORT_NAME,
                            PHOTOC_REVIEW_SHOW_ALL) == -1);
    CHECK(review_model_load(&model, NULL, false, PHOTOC_REVIEW_SORT_NAME,
                            PHOTOC_REVIEW_SHOW_ALL) == -1);
    CHECK(model.items == NULL && model.count == 0);
    free(empty);
    free(growth);
}

int main(void)
{
    const char *temporary = getenv("TMPDIR");
    if (temporary == NULL || temporary[0] == '\0')
        temporary = "/tmp";
    char *root = join(temporary, "photoc-review-model-XXXXXX");
    if (root == NULL)
        return 1;
    if (mkdtemp(root) == NULL) {
        perror("mkdtemp");
        free(root);
        return 1;
    }
    test_discovery(root);
    test_date_sort(root);
    test_navigation_and_counts(root);
    test_empty_growth_and_errors(root);
    CHECK(remove_tree(root) == 0);
    free(root);
    return failures == 0 ? 0 : 1;
}
