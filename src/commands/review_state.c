#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE

#include "review_state.h"

#include "photoc/fs.h"
#include "photoc/json.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define REVIEW_STATE_LIMIT (64u * 1024u * 1024u)

typedef struct {
    const unsigned char *bytes;
    size_t length;
    size_t position;
} json_cursor;

static void skip_space(json_cursor *cursor)
{
    while (cursor->position < cursor->length) {
        unsigned char ch = cursor->bytes[cursor->position];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
            break;
        ++cursor->position;
    }
}

static bool take(json_cursor *cursor, unsigned char expected)
{
    skip_space(cursor);
    if (cursor->position >= cursor->length ||
        cursor->bytes[cursor->position] != expected)
        return false;
    ++cursor->position;
    return true;
}

static int hex_digit(unsigned char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

static int unicode_unit(json_cursor *cursor, unsigned int *unit)
{
    if (cursor->length - cursor->position < 4)
        return -1;
    unsigned int value = 0;
    for (size_t i = 0; i < 4; ++i) {
        int digit = hex_digit(cursor->bytes[cursor->position++]);
        if (digit < 0)
            return -1;
        value = (value << 4) | (unsigned int)digit;
    }
    *unit = value;
    return 0;
}

static int append_codepoint(char *output, size_t *length,
                            unsigned int codepoint)
{
    if (codepoint == 0)
        return -1; /* Filesystem path strings cannot contain NUL. */
    if (codepoint < 0x80) {
        output[(*length)++] = (char)codepoint;
    } else if (codepoint < 0x800) {
        output[(*length)++] = (char)(0xc0 | (codepoint >> 6));
        output[(*length)++] = (char)(0x80 | (codepoint & 0x3f));
    } else if (codepoint < 0x10000) {
        output[(*length)++] = (char)(0xe0 | (codepoint >> 12));
        output[(*length)++] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        output[(*length)++] = (char)(0x80 | (codepoint & 0x3f));
    } else {
        output[(*length)++] = (char)(0xf0 | (codepoint >> 18));
        output[(*length)++] = (char)(0x80 | ((codepoint >> 12) & 0x3f));
        output[(*length)++] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        output[(*length)++] = (char)(0x80 | (codepoint & 0x3f));
    }
    return 0;
}

static char *parse_string(json_cursor *cursor)
{
    if (!take(cursor, '"'))
        return NULL;
    size_t start = cursor->position;
    size_t closing = start;
    bool escaped = false;
    while (closing < cursor->length) {
        unsigned char ch = cursor->bytes[closing];
        if (ch == '"' && !escaped)
            break;
        if (ch == '\\' && !escaped)
            escaped = true;
        else
            escaped = false;
        ++closing;
    }
    if (closing == cursor->length) {
        errno = EINVAL;
        return NULL;
    }
    size_t capacity = closing - start + 1;
    char *result = calloc(capacity, 1);
    if (result == NULL)
        return NULL;
    size_t used = 0;
    while (cursor->position < cursor->length) {
        unsigned char ch = cursor->bytes[cursor->position++];
        if (ch == '"') {
            result[used] = '\0';
            if (!photoc_json_is_valid_utf8(result))
                break;
            return result;
        }
        if (ch < 0x20)
            break;
        if (ch != '\\') {
            result[used++] = (char)ch;
            continue;
        }
        if (cursor->position >= cursor->length)
            break;
        ch = cursor->bytes[cursor->position++];
        switch (ch) {
        case '"':
        case '\\':
        case '/':
            result[used++] = (char)ch;
            break;
        case 'b':
            result[used++] = '\b';
            break;
        case 'f':
            result[used++] = '\f';
            break;
        case 'n':
            result[used++] = '\n';
            break;
        case 'r':
            result[used++] = '\r';
            break;
        case 't':
            result[used++] = '\t';
            break;
        case 'u': {
            unsigned int unit;
            if (unicode_unit(cursor, &unit) != 0)
                goto invalid;
            if (unit == 0)
                goto invalid;
            if (unit >= 0xd800 && unit <= 0xdbff) {
                if (cursor->length - cursor->position < 2 ||
                    cursor->bytes[cursor->position] != '\\' ||
                    cursor->bytes[cursor->position + 1] != 'u')
                    goto invalid;
                cursor->position += 2;
                unsigned int low;
                if (unicode_unit(cursor, &low) != 0 || low < 0xdc00 ||
                    low > 0xdfff)
                    goto invalid;
                unit = 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00);
            } else if (unit >= 0xdc00 && unit <= 0xdfff) {
                goto invalid;
            }
            if (append_codepoint(result, &used, unit) != 0)
                goto invalid;
            break;
        }
        default:
            goto invalid;
        }
    }
invalid:
    free(result);
    errno = EINVAL;
    return NULL;
}

static char *decode_hex(const char *value)
{
    size_t length = strlen(value);
    if (length == 0 || (length & 1u) != 0) {
        errno = EINVAL;
        return NULL;
    }
    char *result = malloc(length / 2 + 1);
    if (result == NULL)
        return NULL;
    for (size_t i = 0; i + 1 < length; i += 2) {
        int high = hex_digit((unsigned char)value[i]);
        int low = hex_digit((unsigned char)value[i + 1]);
        if (high < 0 || low < 0 || (high == 0 && low == 0)) {
            free(result);
            errno = EINVAL;
            return NULL;
        }
        result[i / 2] = (char)((high << 4) | low);
    }
    result[length / 2] = '\0';
    return result;
}

static bool valid_relative_path(const char *path)
{
    if (path == NULL || path[0] == '\0' || path[0] == '/')
        return false;
    const char *component = path;
    while (*component != '\0') {
        const char *slash = strchr(component, '/');
        size_t length =
            slash == NULL ? strlen(component) : (size_t)(slash - component);
        if (length == 0 || (length == 1 && component[0] == '.') ||
            (length == 2 && component[0] == '.' && component[1] == '.'))
            return false;
        if (slash == NULL)
            break;
        component = slash + 1;
    }
    return path[strlen(path) - 1] != '/';
}

static int parse_item(json_cursor *cursor, review_state_entry *entry)
{
    if (!take(cursor, '{'))
        goto invalid;
    bool have_path = false;
    bool have_status = false;
    bool hex_path = false;
    do {
        char *key = parse_string(cursor);
        if (key == NULL)
            return -1;
        if (!take(cursor, ':')) {
            free(key);
            goto invalid;
        }
        char *value = parse_string(cursor);
        if (value == NULL) {
            free(key);
            return -1;
        }
        if (strcmp(key, "path") == 0 || strcmp(key, "path_bytes_hex") == 0) {
            if (have_path) {
                free(key);
                free(value);
                goto invalid;
            }
            hex_path = strcmp(key, "path_bytes_hex") == 0;
            entry->path = hex_path ? decode_hex(value) : strdup(value);
            if (entry->path == NULL) {
                free(key);
                free(value);
                return -1;
            }
            have_path = true;
        } else if (strcmp(key, "status") == 0) {
            if (have_status) {
                free(key);
                free(value);
                goto invalid;
            }
            if (strcmp(value, "picked") == 0)
                entry->status = PHOTOC_REVIEW_PICKED;
            else if (strcmp(value, "rejected") == 0)
                entry->status = PHOTOC_REVIEW_REJECTED;
            else {
                free(key);
                free(value);
                goto invalid;
            }
            have_status = true;
        } else {
            free(key);
            free(value);
            goto invalid;
        }
        free(key);
        free(value);
        if (take(cursor, '}'))
            break;
        if (!take(cursor, ','))
            goto invalid;
    } while (true);
    if (!have_path || !have_status || !valid_relative_path(entry->path) ||
        (hex_path && photoc_json_is_valid_utf8(entry->path)))
        goto invalid;
    return 0;
invalid:
    errno = EINVAL;
    return -1;
}

static int compare_entry(const void *left, const void *right)
{
    const review_state_entry *a = left;
    const review_state_entry *b = right;
    return strcmp(a->path, b->path);
}

static void free_entries(review_state_entry *entries, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        free(entries[i].path);
    free(entries);
}

static int parse_items(json_cursor *cursor, review_state_entry **entries,
                       size_t *count)
{
    if (!take(cursor, '['))
        goto invalid;
    if (take(cursor, ']'))
        return 0;
    size_t capacity = 0;
    do {
        if (*count == capacity) {
            size_t next = capacity == 0 ? 16 : capacity * 2;
            if (next < capacity || next > SIZE_MAX / sizeof(**entries)) {
                errno = EOVERFLOW;
                return -1;
            }
            review_state_entry *grown =
                realloc(*entries, next * sizeof(**entries));
            if (grown == NULL)
                return -1;
            *entries = grown;
            capacity = next;
        }
        (*entries)[*count] = (review_state_entry){0};
        if (parse_item(cursor, &(*entries)[*count]) != 0) {
            free((*entries)[*count].path);
            return -1;
        }
        ++*count;
        if (take(cursor, ']'))
            break;
        if (!take(cursor, ','))
            goto invalid;
    } while (true);
    qsort(*entries, *count, sizeof(**entries), compare_entry);
    for (size_t i = 1; i < *count; ++i) {
        if (strcmp((*entries)[i - 1].path, (*entries)[i].path) == 0)
            goto invalid;
    }
    return 0;
invalid:
    errno = EINVAL;
    return -1;
}

static int parse_state(const char *bytes, size_t length, const char *root,
                       review_state_entry **entries, size_t *count)
{
    json_cursor cursor = {.bytes = (const unsigned char *)bytes,
                          .length = length};
    char *file_root = NULL;
    if (!take(&cursor, '{'))
        goto invalid;
    bool have_version = false, have_root = false, have_items = false;
    bool hex_root = false;
    do {
        char *key = parse_string(&cursor);
        if (key == NULL)
            goto fail;
        if (!take(&cursor, ':')) {
            free(key);
            goto invalid;
        }
        if (strcmp(key, "version") == 0 && !have_version) {
            skip_space(&cursor);
            if (cursor.position >= cursor.length ||
                cursor.bytes[cursor.position++] != '1') {
                free(key);
                errno = ENOTSUP;
                goto fail;
            }
            have_version = true;
        } else if ((strcmp(key, "root") == 0 ||
                    strcmp(key, "root_bytes_hex") == 0) &&
                   !have_root) {
            hex_root = strcmp(key, "root_bytes_hex") == 0;
            char *value = parse_string(&cursor);
            if (value == NULL) {
                free(key);
                goto fail;
            }
            file_root = hex_root ? decode_hex(value) : strdup(value);
            free(value);
            if (file_root == NULL) {
                free(key);
                goto fail;
            }
            have_root = true;
        } else if (strcmp(key, "items") == 0 && !have_items) {
            if (parse_items(&cursor, entries, count) != 0) {
                free(key);
                goto fail;
            }
            have_items = true;
        } else {
            free(key);
            goto invalid;
        }
        free(key);
        if (take(&cursor, '}'))
            break;
        if (!take(&cursor, ','))
            goto invalid;
    } while (true);
    skip_space(&cursor);
    if (!have_version || !have_root || !have_items ||
        cursor.position != cursor.length ||
        (hex_root && photoc_json_is_valid_utf8(file_root)))
        goto invalid;
    if (strcmp(file_root, root) != 0) {
        free(file_root);
        errno = EXDEV;
        return -1;
    }
    free(file_root);
    return 0;
invalid:
    errno = EINVAL;
fail: {
    int saved_errno = errno;
    free(file_root);
    errno = saved_errno;
    return -1;
}
}

static int read_state_file(int directory_fd, const char *filename, char **bytes,
                           size_t *length, bool *exists)
{
    struct stat prior;
    if (fstatat(directory_fd, filename, &prior, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) {
            *exists = false;
            return 0;
        }
        return -1;
    }
    if (!S_ISREG(prior.st_mode)) {
        errno = EINVAL;
        return -1;
    }
    int fd = openat(directory_fd, filename, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return -1;
    struct stat info;
    if (fstat(fd, &info) != 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    if (!S_ISREG(info.st_mode) || info.st_size < 0 ||
        (uintmax_t)info.st_size > REVIEW_STATE_LIMIT) {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    size_t size = (size_t)info.st_size;
    char *buffer = malloc(size + 1);
    if (buffer == NULL) {
        close(fd);
        return -1;
    }
    size_t used = 0;
    while (used < size) {
        ssize_t got = read(fd, buffer + used, size - used);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0) {
            int saved_errno = got == 0 ? EIO : errno;
            free(buffer);
            close(fd);
            errno = saved_errno;
            return -1;
        }
        used += (size_t)got;
    }
    char extra;
    ssize_t more;
    do {
        more = read(fd, &extra, 1);
    } while (more < 0 && errno == EINTR);
    int saved_errno = errno;
    if (close(fd) != 0 && more == 0) {
        more = -1;
        saved_errno = errno;
    }
    if (more != 0) {
        free(buffer);
        errno = more > 0 ? EIO : saved_errno;
        return -1;
    }
    buffer[size] = '\0';
    *bytes = buffer;
    *length = size;
    *exists = true;
    return 0;
}

static int split_state_path(const char *path, char **directory, char **filename)
{
    const char *slash = strrchr(path, '/');
    const char *leaf = slash == NULL ? path : slash + 1;
    if (leaf[0] == '\0' || strcmp(leaf, ".") == 0 || strcmp(leaf, "..") == 0) {
        errno = EINVAL;
        return -1;
    }
    char *parent;
    if (slash == NULL) {
        parent = strdup(".");
    } else if (slash == path) {
        parent = strdup("/");
    } else {
        parent = strndup(path, (size_t)(slash - path));
    }
    if (parent == NULL)
        return -1;
    char *canonical = realpath(parent, NULL);
    free(parent);
    if (canonical == NULL)
        return -1;
    char *copy = strdup(leaf);
    if (copy == NULL) {
        free(canonical);
        return -1;
    }
    *directory = canonical;
    *filename = copy;
    return 0;
}

int review_state_open(review_state *out, const char *root,
                      const char *state_path)
{
    if (out == NULL || root == NULL || root[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    /* The caller supplies a zero-initialized or cleaned state. Populate it
       directly so every failure path releases exactly the owned fields. */
    out->directory_fd = -1;
    out->root = realpath(root, NULL);
    if (out->root == NULL)
        goto fail;
    struct stat root_info;
    if (stat(out->root, &root_info) != 0)
        goto fail;
    if (!S_ISDIR(root_info.st_mode)) {
        errno = ENOTDIR;
        goto fail;
    }
    char *default_path = NULL;
    if (state_path == NULL) {
        if (photoc_fs_join(out->root, ".photoc-review.json", &default_path) !=
            0)
            goto fail;
        state_path = default_path;
    }
    int result = split_state_path(state_path, &out->directory, &out->filename);
    free(default_path);
    if (result != 0)
        goto fail;
    out->directory_fd =
        open(out->directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (out->directory_fd < 0)
        goto fail;
    if (read_state_file(out->directory_fd, out->filename, &out->snapshot,
                        &out->snapshot_length, &out->exists) != 0)
        goto fail;
    if (out->exists) {
        if (parse_state(out->snapshot, out->snapshot_length, out->root,
                        &out->entries, &out->count) != 0)
            goto fail;
    }
    return 0;
fail: {
    int saved_errno = errno;
    review_state_cleanup(out);
    errno = saved_errno;
    return -1;
}
}

static const review_state_entry *find_entry(const review_state *state,
                                            const char *path)
{
    if (state->count == 0)
        return NULL;
    review_state_entry key = {.path = (char *)path};
    return bsearch(&key, state->entries, state->count, sizeof(*state->entries),
                   compare_entry);
}

int review_state_apply(const review_state *state, review_model *model)
{
    if (state == NULL || model == NULL ||
        (model->count != 0 && model->items == NULL)) {
        errno = EINVAL;
        return -1;
    }
    for (size_t i = 0; i < model->count; ++i) {
        const review_state_entry *entry =
            find_entry(state, model->items[i].relative_path);
        model->items[i].status =
            entry == NULL ? PHOTOC_REVIEW_UNMARKED : entry->status;
    }
    return review_model_rebuild(model);
}

static int write_hex(FILE *stream, const char *value)
{
    static const char digits[] = "0123456789abcdef";
    if (fputc('"', stream) == EOF)
        return -1;
    for (const unsigned char *p = (const unsigned char *)value; *p != 0; ++p) {
        if (fputc(digits[*p >> 4], stream) == EOF ||
            fputc(digits[*p & 15], stream) == EOF)
            return -1;
    }
    return fputc('"', stream) == EOF ? -1 : 0;
}

static int write_path_field(FILE *stream, const char *normal_key,
                            const char *hex_key, const char *value)
{
    bool valid = photoc_json_is_valid_utf8(value);
    if (fprintf(stream, "\"%s\":", valid ? normal_key : hex_key) < 0)
        return -1;
    return valid ? photoc_json_write_string(stream, value)
                 : write_hex(stream, value);
}

static int write_state(FILE *stream, const char *root,
                       const review_state_entry *entries, size_t count)
{
    if (fputs("{\"version\":1,", stream) == EOF ||
        write_path_field(stream, "root", "root_bytes_hex", root) != 0 ||
        fputs(",\"items\":[", stream) == EOF)
        return -1;
    for (size_t i = 0; i < count; ++i) {
        if ((i > 0 && fputc(',', stream) == EOF) || fputc('{', stream) == EOF ||
            write_path_field(stream, "path", "path_bytes_hex",
                             entries[i].path) != 0 ||
            fprintf(stream, ",\"status\":\"%s\"}",
                    entries[i].status == PHOTOC_REVIEW_PICKED ? "picked"
                                                              : "rejected") < 0)
            return -1;
    }
    return fputs("]}\n", stream) == EOF ? -1 : 0;
}

static int unchanged_on_disk(const review_state *state)
{
    char *current = NULL;
    size_t length = 0;
    bool exists = false;
    if (read_state_file(state->directory_fd, state->filename, &current, &length,
                        &exists) != 0)
        return -1;
    bool same = exists == state->exists &&
                (!exists || (length == state->snapshot_length &&
                             memcmp(current, state->snapshot, length) == 0));
    free(current);
    if (!same) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

static int random_temp_name(char name[52])
{
    unsigned char bytes[16];
    int random_fd = open("/dev/urandom", O_RDONLY);
    if (random_fd < 0)
        return -1;
    size_t used = 0;
    while (used < sizeof(bytes)) {
        ssize_t got = read(random_fd, bytes + used, sizeof(bytes) - used);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0) {
            int saved_errno = got == 0 ? EIO : errno;
            close(random_fd);
            errno = saved_errno;
            return -1;
        }
        used += (size_t)got;
    }
    if (close(random_fd) != 0)
        return -1;
    static const char digits[] = "0123456789abcdef";
    memcpy(name, ".photoc-review.tmp.", 19);
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        name[19 + i * 2] = digits[bytes[i] >> 4];
        name[20 + i * 2] = digits[bytes[i] & 15];
    }
    name[19 + sizeof(bytes) * 2] = '\0';
    return 0;
}

static int write_atomic(review_state *state, const review_state_entry *entries,
                        size_t count, char **snapshot, size_t *snapshot_length)
{
    if (unchanged_on_disk(state) != 0)
        return -1;
    char temp_leaf[52];
    int fd = -1;
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (random_temp_name(temp_leaf) != 0)
            return -1;
        fd = openat(state->directory_fd, temp_leaf,
                    O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (fd >= 0 || errno != EEXIST)
            break;
    }
    if (fd < 0) {
        return -1;
    }
    struct stat temp_info;
    if (fstat(fd, &temp_info) != 0) {
        int saved_errno = errno;
        close(fd);
        unlinkat(state->directory_fd, temp_leaf, 0);
        errno = saved_errno;
        return -1;
    }
    FILE *stream = fdopen(fd, "w");
    if (stream == NULL) {
        int saved_errno = errno;
        close(fd);
        unlinkat(state->directory_fd, temp_leaf, 0);
        errno = saved_errno;
        return -1;
    }
    int failed = write_state(stream, state->root, entries, count) != 0 ||
                 fflush(stream) != 0 || fsync(fd) != 0;
    int saved_errno = errno;
    if (fclose(stream) != 0) {
        failed = 1;
        saved_errno = errno;
    }
    if (!failed) {
        char *written_bytes = NULL;
        size_t length = 0;
        bool exists = false;
        if (read_state_file(state->directory_fd, temp_leaf, &written_bytes,
                            &length, &exists) != 0 ||
            !exists) {
            failed = 1;
            saved_errno = errno;
        } else {
            *snapshot = written_bytes;
            *snapshot_length = length;
        }
    }
    if (!failed && unchanged_on_disk(state) != 0) {
        failed = 1;
        saved_errno = errno;
    }
    if (!failed) {
        struct stat current_temp;
        if (fstatat(state->directory_fd, temp_leaf, &current_temp,
                    AT_SYMLINK_NOFOLLOW) != 0) {
            failed = 1;
            saved_errno = errno;
        } else if (!S_ISREG(current_temp.st_mode) ||
                   current_temp.st_dev != temp_info.st_dev ||
                   current_temp.st_ino != temp_info.st_ino) {
            failed = 1;
            saved_errno = EBUSY;
        }
    }
    if (!failed) {
        int rename_result = state->exists
                                ? renameat(state->directory_fd, temp_leaf,
                                           state->directory_fd, state->filename)
                                : photoc_fs_renameat_noreplace(
                                      state->directory_fd, temp_leaf,
                                      state->directory_fd, state->filename);
        if (rename_result != 0) {
            failed = 1;
            saved_errno = errno;
        }
    }
    if (failed) {
        unlinkat(state->directory_fd, temp_leaf, 0);
        free(*snapshot);
        *snapshot = NULL;
        *snapshot_length = 0;
    }
    if (failed) {
        errno = saved_errno == 0 ? EIO : saved_errno;
        return -1;
    }
    return 0;
}

int review_state_save_mark(review_state *state, const char *relative_path,
                           photoc_review_status status)
{
    if (state == NULL || state->directory_fd < 0 ||
        !valid_relative_path(relative_path) ||
        (status != PHOTOC_REVIEW_UNMARKED && status != PHOTOC_REVIEW_PICKED &&
         status != PHOTOC_REVIEW_REJECTED)) {
        errno = EINVAL;
        return -1;
    }
    size_t found = state->count;
    for (size_t i = 0; i < state->count; ++i) {
        if (strcmp(state->entries[i].path, relative_path) == 0) {
            found = i;
            break;
        }
    }
    if ((found == state->count && status == PHOTOC_REVIEW_UNMARKED) ||
        (found < state->count && state->entries[found].status == status))
        return 0;
    bool insert = found == state->count;
    bool remove = status == PHOTOC_REVIEW_UNMARKED;
    size_t next_count = state->count + (insert ? 1u : 0u) - (remove ? 1u : 0u);
    review_state_entry *next =
        calloc(next_count == 0 ? 1 : next_count, sizeof(*next));
    if (next == NULL)
        return -1;
    char *new_path = NULL;
    if (insert) {
        new_path = strdup(relative_path);
        if (new_path == NULL) {
            free(next);
            return -1;
        }
    }
    size_t used = 0;
    for (size_t i = 0; i < state->count; ++i) {
        if (i == found && remove)
            continue;
        next[used] = state->entries[i];
        if (i == found)
            next[used].status = status;
        ++used;
    }
    if (insert)
        next[used++] = (review_state_entry){.path = new_path, .status = status};
    qsort(next, next_count, sizeof(*next), compare_entry);
    char *snapshot = NULL;
    size_t snapshot_length = 0;
    if (write_atomic(state, next, next_count, &snapshot, &snapshot_length) !=
        0) {
        free(new_path);
        free(next);
        return -1;
    }
    if (remove)
        free(state->entries[found].path);
    free(state->entries);
    state->entries = next;
    state->count = next_count;
    free(state->snapshot);
    state->snapshot = snapshot;
    state->snapshot_length = snapshot_length;
    state->exists = true;
    return 0;
}

void review_state_cleanup(review_state *state)
{
    if (state == NULL)
        return;
    free(state->root);
    free(state->directory);
    free(state->filename);
    free_entries(state->entries, state->count);
    free(state->snapshot);
    if (state->directory_fd >= 0 && state->directory != NULL)
        close(state->directory_fd);
    *state = (review_state){.directory_fd = -1};
}
