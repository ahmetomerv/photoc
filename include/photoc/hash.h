#ifndef PHOTOC_HASH_H
#define PHOTOC_HASH_H

#include <stddef.h>

#define PHOTOC_SHA256_DIGEST_SIZE 32
#define PHOTOC_SHA256_HEX_SIZE 65

/* SHA-256 block compression uses a runtime-detected hardware path when the
   CPU provides it: the Intel SHA extensions on x86_64, or the ARMv8 SHA-2
   instructions on AArch64. The portable scalar path is always the fallback.
   Set PHOTOC_SHA256_FORCE_SCALAR in the environment (or define it when
   compiling) to force the scalar path for testing; the digest is identical. */

/* Hashes a regular file as raw bytes using SHA-256. A symlink at the final
   path component and non-regular files are rejected. Returns 0 on success,
   or -1 with errno set on invalid arguments, filesystem/read errors, or
   files too large for SHA-256's 64-bit length field. out is caller-owned
   and is unchanged on failure. */
int photoc_hash_file_sha256(const char *path,
                            unsigned char out[PHOTOC_SHA256_DIGEST_SIZE]);

/* Hashes an in-memory buffer. length may be 0. Returns 0 on success, or -1
   with errno EINVAL for NULL data when length != 0, NULL out, or a length
   that overflows SHA-256's 64-bit bit-counter. */
int photoc_hash_bytes_sha256(const unsigned char *data, size_t length,
                             unsigned char out[PHOTOC_SHA256_DIGEST_SIZE]);

/* Writes lowercase hexadecimal and a terminating NUL to caller-owned out.
   Returns 0 on success, or -1 with errno EINVAL for NULL pointers. */
int photoc_hash_sha256_hex(
    const unsigned char digest[PHOTOC_SHA256_DIGEST_SIZE],
    char out[PHOTOC_SHA256_HEX_SIZE]);

#endif
