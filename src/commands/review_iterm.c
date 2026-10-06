#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE

#include "review_iterm.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool review_iterm_auto_supported(const char *term_program,
                                 const char *session_id, const char *term,
                                 const char *tmux, const char *sty)
{
    return term_program != NULL && strcmp(term_program, "iTerm.app") == 0 &&
           session_id != NULL && session_id[0] != '\0' &&
           (term == NULL || (strncmp(term, "screen", 6) != 0 &&
                             strncmp(term, "tmux", 4) != 0 &&
                             strcmp(term, "dumb") != 0)) &&
           (tmux == NULL || tmux[0] == '\0') &&
           (sty == NULL || sty[0] == '\0');
}

static void encode_triplet(const unsigned char bytes[3], char result[4])
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    result[0] = alphabet[bytes[0] >> 2];
    result[1] = alphabet[((bytes[0] & 3u) << 4) | (bytes[1] >> 4)];
    result[2] = alphabet[((bytes[1] & 15u) << 2) | (bytes[2] >> 6)];
    result[3] = alphabet[bytes[2] & 63u];
}

review_base64_result review_base64_encode(FILE *source, FILE *destination)
{
    unsigned char input[3072];
    unsigned char triplet[3] = {0};
    char output[4096];
    size_t pending = 0;
    size_t produced = 0;
    for (;;) {
        size_t count = fread(input, 1, sizeof(input), source);
        for (size_t i = 0; i < count; ++i) {
            triplet[pending++] = input[i];
            if (pending == 3) {
                encode_triplet(triplet, output + produced);
                produced += 4;
                pending = 0;
                if (produced == sizeof(output)) {
                    if (fwrite(output, 1, produced, destination) != produced)
                        return REVIEW_BASE64_WRITE_ERROR;
                    produced = 0;
                }
            }
        }
        if (count < sizeof(input)) {
            if (ferror(source))
                return REVIEW_BASE64_READ_ERROR;
            break;
        }
    }
    if (pending != 0) {
        for (size_t i = pending; i < 3; ++i)
            triplet[i] = 0;
        encode_triplet(triplet, output + produced);
        output[produced + 2] = pending == 1 ? '=' : output[produced + 2];
        output[produced + 3] = '=';
        produced += 4;
    }
    if (produced != 0 && fwrite(output, 1, produced, destination) != produced)
        return REVIEW_BASE64_WRITE_ERROR;
    return REVIEW_BASE64_OK;
}

static void try_terminate_sequence(FILE *destination)
{
    /* An output failure may have left an OSC sequence open. Best effort only:
       the caller still receives OUTPUT_ERROR and restores terminal state. */
    clearerr(destination);
    (void)fputc('\a', destination);
    (void)fflush(destination);
}

review_image_result review_iterm_render(FILE *destination, const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return REVIEW_IMAGE_UNAVAILABLE;
    struct stat info;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0) {
        close(fd);
        return REVIEW_IMAGE_UNAVAILABLE;
    }
    FILE *source = fdopen(fd, "rb");
    if (source == NULL) {
        close(fd);
        return REVIEW_IMAGE_UNAVAILABLE;
    }

    /* https://iterm2.com/documentation-images.html#protocol */
    if (fprintf(destination,
                "\033]1337;File=inline=1;size=%" PRIuMAX
                ";width=100%%;height=40%%;preserveAspectRatio=1:",
                (uintmax_t)info.st_size) < 0) {
        fclose(source);
        try_terminate_sequence(destination);
        return REVIEW_IMAGE_OUTPUT_ERROR;
    }
    review_base64_result encoded = review_base64_encode(source, destination);
    int saved_errno = errno;
    fclose(source);
    if (encoded == REVIEW_BASE64_WRITE_ERROR) {
        try_terminate_sequence(destination);
        return REVIEW_IMAGE_OUTPUT_ERROR;
    }
    if (fputs("\a\n", destination) == EOF)
        return REVIEW_IMAGE_OUTPUT_ERROR;
    if (encoded == REVIEW_BASE64_READ_ERROR) {
        errno = saved_errno;
        return REVIEW_IMAGE_UNAVAILABLE;
    }
    return REVIEW_IMAGE_RENDERED;
}
