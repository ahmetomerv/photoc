#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

/* Exercise rollback with controlled filesystem changes without exposing
   command internals or adding timing-dependent races to CLI integration tests. */
#include "../src/commands/rename.c"

#include <fcntl.h>
#include <unistd.h>

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static void create_file(const char *path, char content)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0);
    if (fd >= 0) {
        CHECK(write(fd, &content, 1) == 1);
        CHECK(close(fd) == 0);
    }
}

static void check_content(const char *path, char expected)
{
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(fgetc(file) == expected);
        CHECK(fclose(file) == 0);
    }
}

static void test_rollback(int scenario, bool arw)
{
    char directory[] = "/tmp/photoc-rollback-XXXXXX";
    if (mkdtemp(directory) == NULL) {
        CHECK(false);
        return;
    }
    char *source = NULL;
    char *destination = NULL;
    char *held = NULL;
    CHECK(photoc_fs_join(directory, arw ? "source.ARW" : "source.jpg",
                         &source) == 0);
    CHECK(photoc_fs_join(directory, arw ? "destination.ARW" : "destination.jpg",
                         &destination) == 0);
    CHECK(photoc_fs_join(directory, arw ? "held.ARW" : "held.jpg", &held) == 0);
    if (source == NULL || destination == NULL || held == NULL) {
        goto done;
    }
    create_file(source, 'A');
    struct stat info = {0};
    CHECK(lstat(source, &info) == 0);
    rename_entry entry = {.source = source,
                          .destination = destination,
                          .source_device = info.st_dev,
                          .source_inode = info.st_ino,
                          .applied = true};
    rename_plan plan = {.entries = &entry, .count = 1};
    rename_summary summary = {.applied = 1};
    CHECK(photoc_fs_rename_noreplace(source, destination) == 0);
    if (scenario == 1 || scenario == 2) {
        CHECK(photoc_fs_rename_noreplace(destination, held) == 0);
        if (scenario == 1) {
            create_file(destination, 'B');
        } else {
            CHECK(symlink(held, destination) == 0);
        }
    } else if (scenario == 3) {
        create_file(source, 'B');
    }
    rollback_applied(&plan, directory, &summary);
    if (scenario == 0) {
        CHECK(!entry.applied && summary.applied == 0 &&
              summary.rolled_back == 1);
        CHECK(lstat(destination, &info) == -1 && errno == ENOENT);
        check_content(source, 'A');
    } else {
        CHECK(entry.applied && summary.applied == 1 &&
              summary.rolled_back == 0);
        CHECK(lstat(destination, &info) == 0);
        if (scenario == 1 || scenario == 2) {
            CHECK(lstat(source, &info) == -1 && errno == ENOENT);
            check_content(held, 'A');
            if (scenario == 1) {
                check_content(destination, 'B');
            } else {
                CHECK(lstat(destination, &info) == 0 && S_ISLNK(info.st_mode));
            }
        } else {
            check_content(source, 'B');
            check_content(destination, 'A');
        }
    }

done:
    if (source != NULL)
        unlink(source);
    if (destination != NULL)
        unlink(destination);
    if (held != NULL)
        unlink(held);
    free(source);
    free(destination);
    free(held);
    CHECK(rmdir(directory) == 0);
}

int main(void)
{
    for (int scenario = 0; scenario < 4; ++scenario) {
        test_rollback(scenario, false);
        test_rollback(scenario, true);
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
