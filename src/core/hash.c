#if defined(__linux__)
#define _GNU_SOURCE
#elif defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/hash.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* SHA-256 constants and operations follow NIST FIPS 180-4, sections 4-6. */
static const uint32_t round_constants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

typedef struct {
    uint32_t state[8];
    unsigned char block[64];
    size_t block_length;
    uint64_t total_bytes;
} sha256_state;

static uint32_t rotate_right(uint32_t value, unsigned int count)
{
    return (value >> count) | (value << (32u - count));
}

static void process_block(sha256_state *hash, const unsigned char block[64])
{
    uint32_t words[64];
    for (size_t i = 0; i < 16; ++i) {
        words[i] = ((uint32_t)block[i * 4] << 24) |
                   ((uint32_t)block[i * 4 + 1] << 16) |
                   ((uint32_t)block[i * 4 + 2] << 8) |
                   (uint32_t)block[i * 4 + 3];
    }
    for (size_t i = 16; i < 64; ++i) {
        uint32_t a = words[i - 15];
        uint32_t b = words[i - 2];
        uint32_t small_zero = rotate_right(a, 7) ^
                              rotate_right(a, 18) ^ (a >> 3);
        uint32_t small_one = rotate_right(b, 17) ^
                             rotate_right(b, 19) ^ (b >> 10);
        words[i] = words[i - 16] + small_zero + words[i - 7] + small_one;
    }

    uint32_t a = hash->state[0];
    uint32_t b = hash->state[1];
    uint32_t c = hash->state[2];
    uint32_t d = hash->state[3];
    uint32_t e = hash->state[4];
    uint32_t f = hash->state[5];
    uint32_t g = hash->state[6];
    uint32_t h = hash->state[7];
    for (size_t i = 0; i < 64; ++i) {
        uint32_t big_one = rotate_right(e, 6) ^
                           rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t first = h + big_one + choose + round_constants[i] + words[i];
        uint32_t big_zero = rotate_right(a, 2) ^
                            rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t second = big_zero + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    hash->state[0] += a;
    hash->state[1] += b;
    hash->state[2] += c;
    hash->state[3] += d;
    hash->state[4] += e;
    hash->state[5] += f;
    hash->state[6] += g;
    hash->state[7] += h;
}

static void sha256_init(sha256_state *hash)
{
    *hash = (sha256_state){
        .state = {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
        }
    };
}

static void sha256_update(sha256_state *hash, const unsigned char *data,
                          size_t length)
{
    hash->total_bytes += (uint64_t)length;
    while (length != 0) {
        size_t space = sizeof(hash->block) - hash->block_length;
        size_t chunk = length < space ? length : space;
        memcpy(hash->block + hash->block_length, data, chunk);
        hash->block_length += chunk;
        data += chunk;
        length -= chunk;
        if (hash->block_length == sizeof(hash->block)) {
            process_block(hash, hash->block);
            hash->block_length = 0;
        }
    }
}

static void sha256_finish(sha256_state *hash,
                          unsigned char out[PHOTOC_SHA256_DIGEST_SIZE])
{
    uint64_t bit_length = hash->total_bytes * 8u;
    hash->block[hash->block_length++] = 0x80;
    if (hash->block_length > 56) {
        memset(hash->block + hash->block_length, 0,
               sizeof(hash->block) - hash->block_length);
        process_block(hash, hash->block);
        hash->block_length = 0;
    }
    memset(hash->block + hash->block_length, 0, 56 - hash->block_length);
    for (size_t i = 0; i < 8; ++i) {
        hash->block[56 + i] = (unsigned char)(bit_length >> (56 - i * 8));
    }
    process_block(hash, hash->block);
    for (size_t i = 0; i < 8; ++i) {
        out[i * 4] = (unsigned char)(hash->state[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(hash->state[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(hash->state[i] >> 8);
        out[i * 4 + 3] = (unsigned char)hash->state[i];
    }
}

int photoc_hash_file_sha256(const char *path,
                            unsigned char out[PHOTOC_SHA256_DIGEST_SIZE])
{
    if (path == NULL || path[0] == '\0' || out == NULL) {
        errno = EINVAL;
        return -1;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }
    struct stat info;
    if (fstat(fd, &info) != 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    if (!S_ISREG(info.st_mode)) {
        int saved_errno = S_ISDIR(info.st_mode) ? EISDIR : EINVAL;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    sha256_state hash;
    sha256_init(&hash);
    unsigned char buffer[16384];
    for (;;) {
        ssize_t length = read(fd, buffer, sizeof(buffer));
        if (length < 0 && errno == EINTR) {
            continue;
        }
        if (length < 0) {
            int saved_errno = errno;
            close(fd);
            errno = saved_errno;
            return -1;
        }
        if (length == 0) {
            break;
        }
        if ((uint64_t)length > UINT64_MAX / 8u - hash.total_bytes) {
            close(fd);
            errno = EOVERFLOW;
            return -1;
        }
        sha256_update(&hash, buffer, (size_t)length);
    }
    unsigned char digest[PHOTOC_SHA256_DIGEST_SIZE];
    sha256_finish(&hash, digest);
    if (close(fd) != 0) {
        return -1;
    }
    memcpy(out, digest, sizeof(digest));
    return 0;
}

int photoc_hash_sha256_hex(
    const unsigned char digest[PHOTOC_SHA256_DIGEST_SIZE],
    char out[PHOTOC_SHA256_HEX_SIZE])
{
    if (digest == NULL || out == NULL) {
        errno = EINVAL;
        return -1;
    }
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < PHOTOC_SHA256_DIGEST_SIZE; ++i) {
        out[i * 2] = digits[digest[i] >> 4];
        out[i * 2 + 1] = digits[digest[i] & 0x0f];
    }
    out[PHOTOC_SHA256_HEX_SIZE - 1] = '\0';
    return 0;
}
