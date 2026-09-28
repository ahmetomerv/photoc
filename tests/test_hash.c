#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/fs.h"
#include "photoc/hash.h"

#include <errno.h>
#include <stdbool.h>
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

static int write_bytes(const char *path, const void *data, size_t length)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }
    int result =
        length == 0 || fwrite(data, 1, length, file) == length ? 0 : -1;
    if (fclose(file) != 0) {
        result = -1;
    }
    return result;
}

static int write_repeated(const char *path, unsigned char value, size_t length)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }
    unsigned char block[4096];
    memset(block, value, sizeof(block));
    int result = 0;
    while (length != 0) {
        size_t chunk = length < sizeof(block) ? length : sizeof(block);
        if (fwrite(block, 1, chunk, file) != chunk) {
            result = -1;
            break;
        }
        length -= chunk;
    }
    if (fclose(file) != 0) {
        result = -1;
    }
    return result;
}

static void check_digest(const char *path, const char *expected)
{
    unsigned char digest[PHOTOC_SHA256_DIGEST_SIZE];
    char hex[PHOTOC_SHA256_HEX_SIZE];
    if (photoc_hash_file_sha256(path, digest) != 0 ||
        photoc_hash_sha256_hex(digest, hex) != 0) {
        CHECK(false);
        return;
    }
    if (strcmp(hex, expected) != 0) {
        fprintf(stderr, "SHA-256 for %s: expected %s, got %s\n", path, expected,
                hex);
        ++failures;
    }
}

static void test_known_vectors(const char *path)
{
    static const char two_block[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static const unsigned char one_byte[] = {0xbd};
    static const unsigned char four_bytes[] = {0xc9, 0x8c, 0x8e, 0x55};

    CHECK(write_bytes(path, NULL, 0) == 0);
    check_digest(
        path,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(write_bytes(path, "abc", 3) == 0);
    check_digest(
        path,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(write_bytes(path, two_block, sizeof(two_block) - 1) == 0);
    check_digest(
        path,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(write_bytes(path, one_byte, sizeof(one_byte)) == 0);
    check_digest(
        path,
        "68325720aabd7c82f30f554b313d0570c95accbb7dc4b5aae11204c08ffe732b");
    CHECK(write_bytes(path, four_bytes, sizeof(four_bytes)) == 0);
    check_digest(
        path,
        "7abc22c0ae5af26ce93dbb94433a0e0b2e119d014f8e7f65bd56c61ccccd9504");
    CHECK(write_repeated(path, 0, 55) == 0);
    check_digest(
        path,
        "02779466cdec163811d078815c633f21901413081449002f24aa3e80f0b88ef7");
    CHECK(write_repeated(path, 0, 56) == 0);
    check_digest(
        path,
        "d4817aa5497628e7c77e6b606107042bbba3130888c5f47a375e6179be789fbb");
    CHECK(write_repeated(path, 0, 57) == 0);
    check_digest(
        path,
        "65a16cb7861335d5ace3c60718b5052e44660726da4cd13bb745381b235a1785");
    CHECK(write_repeated(path, 0, 64) == 0);
    check_digest(
        path,
        "f5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b");
    CHECK(write_repeated(path, 0, 1000000) == 0);
    check_digest(
        path,
        "d29751f2649b32ff572b5e0a9f541ea660a50f94ff0beedfb0b692b924cc8025");
}

static void test_bytes_hash(void)
{
    unsigned char digest[PHOTOC_SHA256_DIGEST_SIZE];
    char hex[PHOTOC_SHA256_HEX_SIZE];
    CHECK(photoc_hash_bytes_sha256(NULL, 0, digest) == 0);
    CHECK(photoc_hash_sha256_hex(digest, hex) == 0);
    CHECK(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b"
                      "7852b855") == 0);
    CHECK(photoc_hash_bytes_sha256((const unsigned char *)"abc", 3, digest) ==
          0);
    CHECK(photoc_hash_sha256_hex(digest, hex) == 0);
    CHECK(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61"
                      "f20015ad") == 0);
    errno = 0;
    CHECK(photoc_hash_bytes_sha256(NULL, 1, digest) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(photoc_hash_bytes_sha256((const unsigned char *)"x", 1, NULL) == -1 &&
          errno == EINVAL);
}

static void test_same_content(const char *first, const char *second)
{
    unsigned char first_digest[PHOTOC_SHA256_DIGEST_SIZE];
    unsigned char second_digest[PHOTOC_SHA256_DIGEST_SIZE];
    CHECK(write_bytes(first, "same bytes\0including NUL", 24) == 0);
    CHECK(write_bytes(second, "same bytes\0including NUL", 24) == 0);
    CHECK(photoc_hash_file_sha256(first, first_digest) == 0);
    CHECK(photoc_hash_file_sha256(second, second_digest) == 0);
    CHECK(memcmp(first_digest, second_digest, sizeof(first_digest)) == 0);
    CHECK(write_bytes(second, "same bytes\0including NUx", 24) == 0);
    CHECK(photoc_hash_file_sha256(second, second_digest) == 0);
    CHECK(memcmp(first_digest, second_digest, sizeof(first_digest)) != 0);
}

static void test_errors(const char *file, const char *directory,
                        const char *missing, const char *link,
                        const char *pipe_path)
{
    unsigned char digest[PHOTOC_SHA256_DIGEST_SIZE];
    unsigned char sentinel[PHOTOC_SHA256_DIGEST_SIZE];
    memset(sentinel, 0xa5, sizeof(sentinel));
    memcpy(digest, sentinel, sizeof(digest));
    errno = 0;
    CHECK(photoc_hash_file_sha256(NULL, digest) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(photoc_hash_file_sha256("", digest) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(photoc_hash_file_sha256(file, NULL) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(photoc_hash_file_sha256(missing, digest) == -1 && errno == ENOENT);
    errno = 0;
    CHECK(photoc_hash_file_sha256(directory, digest) == -1 && errno == EISDIR);
    CHECK(symlink(file, link) == 0);
    errno = 0;
    CHECK(photoc_hash_file_sha256(link, digest) == -1);
    CHECK(mkfifo(pipe_path, 0600) == 0);
    errno = 0;
    CHECK(photoc_hash_file_sha256(pipe_path, digest) == -1 && errno == EINVAL);
    CHECK(memcmp(digest, sentinel, sizeof(digest)) == 0);

    char hex[PHOTOC_SHA256_HEX_SIZE];
    memset(hex, 'x', sizeof(hex));
    errno = 0;
    CHECK(photoc_hash_sha256_hex(NULL, hex) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(photoc_hash_sha256_hex(digest, NULL) == -1 && errno == EINVAL);
    for (size_t i = 0; i < sizeof(hex); ++i) {
        CHECK(hex[i] == 'x');
    }
    for (size_t i = 0; i < sizeof(digest); ++i) {
        digest[i] = (unsigned char)i;
    }
    CHECK(photoc_hash_sha256_hex(digest, hex) == 0);
    CHECK(strcmp(hex, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b"
                      "1c1d1e1f") == 0);
}

int main(void)
{
    const char *temporary = getenv("TMPDIR");
    if (temporary == NULL || temporary[0] == '\0') {
        temporary = "/tmp";
    }
    char *directory = NULL;
    char *file = NULL;
    char *second = NULL;
    char *missing = NULL;
    char *link = NULL;
    char *pipe_path = NULL;
    if (photoc_fs_join(temporary, "photoc-hash-XXXXXX", &directory) != 0 ||
        mkdtemp(directory) == NULL ||
        photoc_fs_join(directory, "file.bin", &file) != 0 ||
        photoc_fs_join(directory, "second.bin", &second) != 0 ||
        photoc_fs_join(directory, "missing.bin", &missing) != 0 ||
        photoc_fs_join(directory, "link.bin", &link) != 0 ||
        photoc_fs_join(directory, "pipe.bin", &pipe_path) != 0) {
        CHECK(false);
    } else {
        test_known_vectors(file);
        test_bytes_hash();
        test_same_content(file, second);
        test_errors(file, directory, missing, link, pipe_path);
    }
    if (pipe_path != NULL)
        unlink(pipe_path);
    if (link != NULL)
        unlink(link);
    if (file != NULL)
        unlink(file);
    if (second != NULL)
        unlink(second);
    if (directory != NULL)
        rmdir(directory);
    free(link);
    free(pipe_path);
    free(missing);
    free(second);
    free(file);
    free(directory);
    if (failures != 0) {
        fprintf(stderr, "%d hashing test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
