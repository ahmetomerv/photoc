#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE

#include "review_model.h"
#include "review_state.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s (errno=%d)\n", __FILE__, __LINE__, \
                #condition, errno); \
        return false; \
    } \
} while (0)

static char *join(const char *left, const char *right)
{
    size_t length = strlen(left) + strlen(right) + 2;
    char *result = malloc(length);
    if (result != NULL)
        snprintf(result, length, "%s/%s", left, right);
    return result;
}

static bool write_file(const char *path, const char *contents)
{
    FILE *stream = fopen(path, "wb");
    if (stream == NULL)
        return false;
    size_t length = strlen(contents);
    bool okay = fwrite(contents, 1, length, stream) == length;
    return fclose(stream) == 0 && okay;
}

static char *read_file(const char *path)
{
    FILE *stream = fopen(path, "rb");
    if (stream == NULL)
        return NULL;
    if (fseek(stream, 0, SEEK_END) != 0) {
        fclose(stream);
        return NULL;
    }
    long length = ftell(stream);
    if (length < 0 || fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return NULL;
    }
    char *result = malloc((size_t)length + 1);
    if (result == NULL) {
        fclose(stream);
        return NULL;
    }
    if (fread(result, 1, (size_t)length, stream) != (size_t)length ||
        fclose(stream) != 0) {
        free(result);
        return NULL;
    }
    result[length] = '\0';
    return result;
}

static void remove_tree(const char *path)
{
    DIR *directory = opendir(path);
    if (directory == NULL) {
        unlink(path);
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char *child = join(path, entry->d_name);
        if (child != NULL) {
            remove_tree(child);
            free(child);
        }
    }
    closedir(directory);
    rmdir(path);
}

static bool test_save_reload_and_model(const char *root, const char *state_path)
{
    char *first = join(root, "first.JPG");
    char *second = join(root, "second.jpeg");
    CHECK(first != NULL && second != NULL);
    CHECK(write_file(first, "") && write_file(second, ""));

    review_model model = {0};
    CHECK(review_model_load(&model, root, false, PHOTOC_REVIEW_SORT_NAME,
                            PHOTOC_REVIEW_SHOW_ALL) == 0);
    CHECK(model.count == 2 && model.unmarked == 2);
    review_state state = {0};
    CHECK(review_state_open(&state, root, NULL) == 0);
    CHECK(!state.exists && access(state_path, F_OK) != 0);
    CHECK(review_state_apply(&state, &model) == 0);
    CHECK(review_state_save_mark(&state, "first.JPG", PHOTOC_REVIEW_PICKED) == 0);
    CHECK(review_model_mark_current(&model, PHOTOC_REVIEW_PICKED) == 0);
    CHECK(model.picked == 1 && model.unmarked == 1);
    char *json = read_file(state_path);
    CHECK(json != NULL && strstr(json, "\"version\":1") != NULL &&
          strstr(json, "\"path\":\"first.JPG\"") != NULL);
    free(json);
    review_state_cleanup(&state);
    review_model_cleanup(&model);

    CHECK(review_state_open(&state, root, state_path) == 0);
    CHECK(review_model_load(&model, root, false, PHOTOC_REVIEW_SORT_NAME,
                            PHOTOC_REVIEW_SHOW_PICKED) == 0);
    CHECK(review_state_apply(&state, &model) == 0);
    CHECK(model.picked == 1 && model.unmarked == 1 && model.visible_count == 1);
    CHECK(review_state_save_mark(&state, "missing/nested.jpg",
                                 PHOTOC_REVIEW_REJECTED) == 0);
    CHECK(review_state_save_mark(&state, "first.JPG",
                                 PHOTOC_REVIEW_UNMARKED) == 0);
    CHECK(review_state_apply(&state, &model) == 0);
    CHECK(model.picked == 0 && model.unmarked == 2 && model.visible_count == 0);
    CHECK(state.count == 1 && strcmp(state.entries[0].path,
                                     "missing/nested.jpg") == 0);
    review_state_cleanup(&state);
    review_model_cleanup(&model);
    CHECK(review_state_open(&state, root, NULL) == 0);
    CHECK(state.count == 1 && state.entries[0].status == PHOTOC_REVIEW_REJECTED);
    review_state_cleanup(&state);
    free(first);
    free(second);
    return true;
}

static bool expect_invalid(const char *root, const char *path,
                           const char *document)
{
    CHECK(write_file(path, document));
    review_state state = {0};
    CHECK(review_state_open(&state, root, path) != 0);
    review_state_cleanup(&state);
    char *after = read_file(path);
    CHECK(after != NULL && strcmp(after, document) == 0);
    free(after);
    return true;
}

static bool test_rejections(const char *root)
{
    char *path = join(root, "invalid.json");
    CHECK(path != NULL);
    char prefix[4096];
    int n = snprintf(prefix, sizeof(prefix),
                     "{\"version\":1,\"root\":\"%s\",\"items\":", root);
    CHECK(n > 0 && (size_t)n < sizeof(prefix));
    const char *bad_items[] = {
        "[{\"path\":\"a.jpg\",\"status\":\"other\"}]}",
        "[{\"path\":\"../a.jpg\",\"status\":\"picked\"}]}",
        "[{\"path\":\"/a.jpg\",\"status\":\"picked\"}]}",
        "[{\"path\":\"a//b.jpg\",\"status\":\"picked\"}]}",
        "[{\"path\":\"a.jpg\",\"status\":\"picked\"},"
        "{\"path\":\"a.jpg\",\"status\":\"rejected\"}]}",
        "[{\"path\":\"bad\\q.jpg\",\"status\":\"picked\"}]}",
        "[{\"path\":\"bad\\uD800.jpg\",\"status\":\"picked\"}]}",
        "[{\"path_bytes_hex\":\"6100\",\"status\":\"picked\"}]}",
        "[{\"path_bytes_hex\":\"61\",\"status\":\"picked\"}]}",
        "[{\"path\":\"a.jpg\",\"status\":\"picked\"}",
        "[]}" "trailing"
    };
    for (size_t i = 0; i < sizeof(bad_items) / sizeof(bad_items[0]); ++i) {
        char document[8192];
        n = snprintf(document, sizeof(document), "%s%s", prefix, bad_items[i]);
        CHECK(n > 0 && (size_t)n < sizeof(document));
        CHECK(expect_invalid(root, path, document));
    }
    CHECK(expect_invalid(root, path, "{\"version\":2,\"root\":\"/tmp\",\"items\":[]}"));
    CHECK(expect_invalid(root, path, "{\"version\":1,\"root\":\"/unrelated\",\"items\":[]}"));
    CHECK(expect_invalid(root, path, "just another file"));
    char *target = join(root, "symlink-target.json");
    CHECK(target != NULL && write_file(target, "foreign"));
    CHECK(unlink(path) == 0 && symlink(target, path) == 0);
    review_state linked = {0};
    CHECK(review_state_open(&linked, root, path) != 0);
    review_state_cleanup(&linked);
    CHECK(unlink(path) == 0);
    free(target);
    free(path);
    return true;
}

static bool test_filename_roundtrip(const char *root)
{
    char *state_path = join(root, "names.json");
    CHECK(state_path != NULL);
    review_state state = {0};
    CHECK(review_state_open(&state, root, state_path) == 0);
    const char *names[] = {"quote\".jpg", "back\\slash.jpg", "tab\t.jpg",
                           "line\nbreak.jpg", "caf\xc3\xa9.jpg",
                           "nonutf8-\xff.jpg"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        CHECK(review_state_save_mark(&state, names[i], PHOTOC_REVIEW_PICKED) == 0);
    char *json = read_file(state_path);
    CHECK(json != NULL && strstr(json, "path_bytes_hex") != NULL &&
          strstr(json, "\\n") != NULL && strstr(json, "\\t") != NULL &&
          strstr(json, "\\\"") != NULL);
    free(json);
    review_state_cleanup(&state);
    CHECK(review_state_open(&state, root, state_path) == 0);
    CHECK(state.count == sizeof(names) / sizeof(names[0]));
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        bool found = false;
        for (size_t j = 0; j < state.count; ++j)
            found |= strcmp(names[i], state.entries[j].path) == 0;
        CHECK(found);
    }
    review_state_cleanup(&state);
    char *escaped = join(root, "escaped.json");
    CHECK(escaped != NULL);
    char *canonical_root = realpath(root, NULL);
    CHECK(canonical_root != NULL);
    char document[8192];
    int length = snprintf(document, sizeof(document),
                          "{\"version\":1,\"root\":\"%s\",\"items\":["
                          "{\"path\":\"smile-\\uD83D\\uDE00.jpg\",\"status\":\"picked\"}]}  ",
                          canonical_root);
    free(canonical_root);
    CHECK(length > 0 && (size_t)length < sizeof(document));
    CHECK(write_file(escaped, document));
    CHECK(review_state_open(&state, root, escaped) == 0);
    CHECK(state.count == 1 && strcmp(state.entries[0].path,
                                     "smile-\xf0\x9f\x98\x80.jpg") == 0);
    review_state_cleanup(&state);
    free(escaped);
    free(state_path);
    return true;
}

static bool test_non_utf8_root(const char *root)
{
    char *raw_root = join(root, "root-\xff");
    CHECK(raw_root != NULL);
    if (mkdir(raw_root, 0700) != 0 && errno == EILSEQ) {
        free(raw_root); /* This filesystem refuses non-UTF-8 directory names. */
        return true;
    }
    CHECK(access(raw_root, F_OK) == 0);
    char *state_path = join(raw_root, ".photoc-review.json");
    CHECK(state_path != NULL);
    review_state state = {0};
    CHECK(review_state_open(&state, raw_root, NULL) == 0);
    CHECK(review_state_save_mark(&state, "photo.jpg", PHOTOC_REVIEW_PICKED) == 0);
    review_state_cleanup(&state);
    char *json = read_file(state_path);
    CHECK(json != NULL && strstr(json, "root_bytes_hex") != NULL);
    free(json);
    CHECK(review_state_open(&state, raw_root, NULL) == 0);
    CHECK(state.count == 1);
    review_state_cleanup(&state);
    free(state_path);
    free(raw_root);
    return true;
}

static bool no_temporary_files(const char *root)
{
    DIR *directory = opendir(root);
    if (directory == NULL)
        return false;
    bool okay = true;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strstr(entry->d_name, ".tmp.") != NULL)
            okay = false;
    }
    closedir(directory);
    return okay;
}

static bool test_write_failure_cleanup(const char *root)
{
    char *path = join(root, "limited.json");
    CHECK(path != NULL);
    review_state state = {0};
    CHECK(review_state_open(&state, root, path) == 0);
    CHECK(review_state_save_mark(&state, "one.jpg", PHOTOC_REVIEW_PICKED) == 0);
    char *before = read_file(path);
    CHECK(before != NULL);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        struct rlimit limit = {.rlim_cur = 1, .rlim_max = 1};
        signal(SIGXFSZ, SIG_IGN);
        bool okay = setrlimit(RLIMIT_FSIZE, &limit) == 0 &&
                    review_state_save_mark(&state, "two.jpg",
                                           PHOTOC_REVIEW_REJECTED) != 0 &&
                    state.count == 1 && no_temporary_files(root);
        _exit(okay ? 0 : 1);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0);
    char *after = read_file(path);
    CHECK(after != NULL && strcmp(before, after) == 0);
    free(after);
    free(before);
    review_state_cleanup(&state);
    free(path);
    return true;
}

static bool test_failed_save(const char *root)
{
    char *path = join(root, "failure.json");
    char *other = join(root, "other.json");
    char *photo = join(root, "one.jpg");
    CHECK(path != NULL && other != NULL && photo != NULL);
    CHECK(write_file(photo, ""));
    review_state state = {0};
    CHECK(review_state_open(&state, root, path) == 0);
    CHECK(review_state_save_mark(&state, "one.jpg", PHOTOC_REVIEW_PICKED) == 0);
    review_model model = {0};
    CHECK(review_model_load(&model, root, false, PHOTOC_REVIEW_SORT_NAME,
                            PHOTOC_REVIEW_SHOW_ALL) == 0);
    CHECK(review_state_apply(&state, &model) == 0);
    CHECK(model.picked == 1);
    char *before = read_file(path);
    CHECK(before != NULL);
    CHECK(write_file(other, "foreign file"));
    CHECK(rename(other, path) == 0);
    CHECK(review_state_save_mark(&state, "one.jpg", PHOTOC_REVIEW_REJECTED) != 0);
    CHECK(state.count == 1 && strcmp(state.entries[0].path, "one.jpg") == 0);
    CHECK(model.picked == 1 && model.rejected == 0);
    bool one_is_picked = false;
    for (size_t i = 0; i < model.count; ++i) {
        if (strcmp(model.items[i].relative_path, "one.jpg") == 0)
            one_is_picked = model.items[i].status == PHOTOC_REVIEW_PICKED;
    }
    CHECK(one_is_picked);
    char *after = read_file(path);
    CHECK(after != NULL && strcmp(after, "foreign file") == 0);
    free(after);
    CHECK(write_file(path, before));
    CHECK(symlink(other, path) != 0); /* The existing file blocks a symlink. */
    CHECK(unlink(path) == 0);
    CHECK(symlink(other, path) == 0);
    CHECK(review_state_save_mark(&state, "two.jpg", PHOTOC_REVIEW_REJECTED) != 0);
    CHECK(state.count == 1);
    CHECK(unlink(path) == 0);
    CHECK(write_file(path, before));
    review_state_cleanup(&state);
    review_state reopened = {0};
    CHECK(review_state_open(&reopened, root, path) == 0);
    CHECK(reopened.count == 1);
    review_state_cleanup(&reopened);
    review_model_cleanup(&model);
    free(before);
    free(path);
    free(other);
    free(photo);
    return true;
}

int main(void)
{
    char template[] = "/tmp/photoc-review-state-XXXXXX";
    char *root = mkdtemp(template);
    if (root == NULL)
        return 1;
    char *state_path = join(root, ".photoc-review.json");
    bool okay = state_path != NULL &&
                test_save_reload_and_model(root, state_path) &&
                test_rejections(root) &&
                test_filename_roundtrip(root) &&
                test_non_utf8_root(root) &&
                test_write_failure_cleanup(root) &&
                test_failed_save(root);
    free(state_path);
    remove_tree(root);
    return okay ? 0 : 1;
}
