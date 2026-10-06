#define _POSIX_C_SOURCE 200809L

#include "review_iterm.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s (errno=%d)\n", __FILE__, __LINE__, \
                #condition, errno); \
        exit(1); \
    } \
} while (0)

static char *contents(FILE *stream)
{
    if (fflush(stream) != 0 || fseek(stream, 0, SEEK_END) != 0)
        return NULL;
    long length = ftell(stream);
    if (length < 0 || fseek(stream, 0, SEEK_SET) != 0)
        return NULL;
    char *result = malloc((size_t)length + 1);
    if (result == NULL)
        return NULL;
    if (fread(result, 1, (size_t)length, stream) != (size_t)length) {
        free(result);
        return NULL;
    }
    result[length] = '\0';
    return result;
}

static int encode_bytes(const unsigned char *bytes, size_t length,
                        const char *expected)
{
    FILE *source = tmpfile();
    FILE *destination = tmpfile();
    CHECK(source != NULL && destination != NULL);
    CHECK(fwrite(bytes, 1, length, source) == length);
    CHECK(fseek(source, 0, SEEK_SET) == 0);
    CHECK(review_base64_encode(source, destination) == REVIEW_BASE64_OK);
    char *actual = contents(destination);
    CHECK(actual != NULL);
    CHECK(strcmp(actual, expected) == 0);
    free(actual);
    fclose(source);
    fclose(destination);
    return 0;
}

static int test_vectors(void)
{
    static const char *plain[] = {
        "", "f", "fo", "foo", "foob", "fooba", "foobar"
    };
    static const char *encoded[] = {
        "", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"
    };
    for (size_t i = 0; i < sizeof(plain) / sizeof(*plain); ++i)
        CHECK(encode_bytes((const unsigned char *)plain[i], strlen(plain[i]),
                           encoded[i]) == 0);
    return 0;
}

static int test_chunk_boundaries(void)
{
    for (size_t length = 3070; length <= 3075; ++length) {
        unsigned char bytes[3075];
        memset(bytes, 'f', length);
        size_t groups = (length + 2) / 3;
        char *expected = malloc(groups * 4 + 1);
        CHECK(expected != NULL);
        for (size_t i = 0; i < groups; ++i)
            memcpy(expected + i * 4, "ZmZm", 4);
        if (length % 3 == 1)
            memcpy(expected + (groups - 1) * 4, "Zg==", 4);
        else if (length % 3 == 2)
            memcpy(expected + (groups - 1) * 4, "ZmY=", 4);
        expected[groups * 4] = '\0';
        CHECK(encode_bytes(bytes, length, expected) == 0);
        free(expected);
    }
    return 0;
}

static int test_write_error(void)
{
    FILE *source = tmpfile();
    FILE *destination = tmpfile();
    CHECK(source != NULL && destination != NULL);
    CHECK(fputs("foo", source) >= 0 && fseek(source, 0, SEEK_SET) == 0);
    CHECK(setvbuf(destination, NULL, _IONBF, 0) == 0);
    CHECK(close(fileno(destination)) == 0);
    CHECK(review_base64_encode(source, destination) == REVIEW_BASE64_WRITE_ERROR);
    fclose(source);
    fclose(destination);

    destination = tmpfile();
    CHECK(destination != NULL);
    CHECK(setvbuf(destination, NULL, _IONBF, 0) == 0);
    CHECK(close(fileno(destination)) == 0);
    CHECK(review_iterm_render(destination, PHOTOC_REVIEW_FIXTURES "/flat.jpg") ==
          REVIEW_IMAGE_OUTPUT_ERROR);
    fclose(destination);
    return 0;
}

static int test_protocol(void)
{
    const char *path = PHOTOC_REVIEW_FIXTURES "/flat.jpg";
    struct stat info;
    CHECK(stat(path, &info) == 0);
    FILE *destination = tmpfile();
    CHECK(destination != NULL);
    CHECK(review_iterm_render(destination, PHOTOC_REVIEW_FIXTURES "/missing.jpg") ==
          REVIEW_IMAGE_UNAVAILABLE);
    CHECK(ftell(destination) == 0);
    CHECK(review_iterm_render(destination, path) == REVIEW_IMAGE_RENDERED);
    char *rendered = contents(destination);
    CHECK(rendered != NULL);
    char prefix[160];
    snprintf(prefix, sizeof(prefix),
             "\033]1337;File=inline=1;size=%lld;width=100%%;height=40%%;"
             "preserveAspectRatio=1:", (long long)info.st_size);
    size_t prefix_length = strlen(prefix);
    size_t rendered_length = strlen(rendered);
    CHECK(strncmp(rendered, prefix, prefix_length) == 0);
    CHECK(rendered_length > prefix_length + 2);
    CHECK(strcmp(rendered + rendered_length - 2, "\a\n") == 0);

    FILE *source = fopen(path, "rb");
    FILE *encoded = tmpfile();
    CHECK(source != NULL && encoded != NULL);
    CHECK(review_base64_encode(source, encoded) == REVIEW_BASE64_OK);
    char *payload = contents(encoded);
    CHECK(payload != NULL);
    CHECK(strlen(payload) == rendered_length - prefix_length - 2);
    CHECK(memcmp(rendered + prefix_length, payload, strlen(payload)) == 0);
    free(payload);
    free(rendered);
    fclose(source);
    fclose(encoded);
    fclose(destination);
    return 0;
}

static int test_detection(void)
{
    CHECK(review_iterm_auto_supported("iTerm.app", "session", "xterm-256color",
                                      NULL, NULL));
    CHECK(!review_iterm_auto_supported(NULL, "session", NULL, NULL, NULL));
    CHECK(!review_iterm_auto_supported("xterm", "session", NULL, NULL, NULL));
    CHECK(!review_iterm_auto_supported("iTerm.app", NULL, NULL, NULL, NULL));
    CHECK(!review_iterm_auto_supported("iTerm.app", "", NULL, NULL, NULL));
    CHECK(!review_iterm_auto_supported("iTerm.app", "session", "screen-256color",
                                       NULL, NULL));
    CHECK(!review_iterm_auto_supported("iTerm.app", "session", "tmux-256color",
                                       NULL, NULL));
    CHECK(!review_iterm_auto_supported("iTerm.app", "session", "dumb",
                                       NULL, NULL));
    CHECK(!review_iterm_auto_supported("iTerm.app", "session", NULL,
                                       "tmux", NULL));
    CHECK(!review_iterm_auto_supported("iTerm.app", "session", NULL,
                                       NULL, "screen"));
    return 0;
}

int main(void)
{
    if (test_vectors() != 0 || test_chunk_boundaries() != 0 ||
        test_write_error() != 0 || test_protocol() != 0 ||
        test_detection() != 0)
        return 1;
    return 0;
}
