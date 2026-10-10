#include "photoc/filename_template.h"

#include "photoc/fs.h"
#include "photoc/timestamp.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TEMPLATE_TOKEN_DATE,
    TEMPLATE_TOKEN_DATETIME,
    TEMPLATE_TOKEN_CAMERA,
    TEMPLATE_TOKEN_MAKE,
    TEMPLATE_TOKEN_ISO,
    TEMPLATE_TOKEN_APERTURE,
    TEMPLATE_TOKEN_FOCAL,
    TEMPLATE_TOKEN_SEQUENCE,
    TEMPLATE_TOKEN_ORIGINAL,
    TEMPLATE_TOKEN_EXT
} template_token;

/* A compiled pattern is a sequence of sanitized literal chunks and token
   references. Literal bytes live in one owned pool; chunks index into it. */
typedef struct {
    bool is_token;
    template_token token; /* Valid when is_token. */
    size_t offset;        /* Opening brace in the source pattern. */
    size_t literal_start;
    size_t literal_length;
} template_chunk;

struct photoc_filename_template {
    char *literals; /* Owned, already sanitized and brace-decoded. */
    size_t literals_length;
    template_chunk *chunks; /* Owned. */
    size_t count;
};

static bool template_char_is_safe(unsigned char byte)
{
    return byte >= 0x20 && byte != 0x7f && byte != '/' && byte != '\\' &&
           byte != ':' && byte != '*' && byte != '?' && byte != '"' &&
           byte != '<' && byte != '>' && byte != '|';
}

static char sanitize_char(char ch)
{
    return template_char_is_safe((unsigned char)ch) ? ch : '_';
}

static bool token_kind(const char *token, size_t length, template_token *kind)
{
    static const struct {
        const char *name;
        template_token kind;
    } table[] = {
        {"date", TEMPLATE_TOKEN_DATE},
        {"datetime", TEMPLATE_TOKEN_DATETIME},
        {"camera", TEMPLATE_TOKEN_CAMERA},
        {"make", TEMPLATE_TOKEN_MAKE},
        {"iso", TEMPLATE_TOKEN_ISO},
        {"aperture", TEMPLATE_TOKEN_APERTURE},
        {"focal", TEMPLATE_TOKEN_FOCAL},
        {"sequence", TEMPLATE_TOKEN_SEQUENCE},
        {"original", TEMPLATE_TOKEN_ORIGINAL},
        {"ext", TEMPLATE_TOKEN_EXT},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (strlen(table[i].name) == length &&
            memcmp(token, table[i].name, length) == 0) {
            if (kind != NULL) {
                *kind = table[i].kind;
            }
            return true;
        }
    }
    return false;
}

photoc_template_result photoc_filename_template_validate(const char *pattern,
                                                         size_t *error_offset)
{
    if (error_offset != NULL) {
        *error_offset = SIZE_MAX;
    }
    if (pattern == NULL) {
        return PHOTOC_TEMPLATE_INVALID_ARGUMENT;
    }
    if (pattern[0] == '\0') {
        return PHOTOC_TEMPLATE_INVALID_TEMPLATE;
    }
    if (strcmp(pattern, ".") == 0 || strcmp(pattern, "..") == 0) {
        return PHOTOC_TEMPLATE_INVALID_TEMPLATE;
    }
    for (size_t i = 0; pattern[i] != '\0';) {
        if ((pattern[i] == '{' && pattern[i + 1] == '{') ||
            (pattern[i] == '}' && pattern[i + 1] == '}')) {
            i += 2;
        } else if (pattern[i] == '{') {
            size_t start = i++;
            size_t token_start = i;
            while (pattern[i] != '\0' && pattern[i] != '}' &&
                   pattern[i] != '{') {
                ++i;
            }
            if (error_offset != NULL) {
                *error_offset = start;
            }
            if (pattern[i] != '}' || i == token_start) {
                return PHOTOC_TEMPLATE_INVALID_TEMPLATE;
            }
            if (!token_kind(pattern + token_start, i - token_start, NULL)) {
                return PHOTOC_TEMPLATE_UNKNOWN_PLACEHOLDER;
            }
            if (error_offset != NULL) {
                *error_offset = SIZE_MAX;
            }
            ++i;
        } else if (pattern[i] == '}') {
            if (error_offset != NULL) {
                *error_offset = i;
            }
            return PHOTOC_TEMPLATE_INVALID_TEMPLATE;
        } else {
            ++i;
        }
    }
    return PHOTOC_TEMPLATE_OK;
}

static bool template_append_literal(photoc_filename_template *compiled,
                                    size_t *capacity, char ch)
{
    if (compiled->literals_length == SIZE_MAX) {
        return false;
    }
    size_t needed = compiled->literals_length + 1;
    if (needed > *capacity) {
        size_t next = *capacity == 0 ? 64 : *capacity;
        while (next < needed) {
            if (next > SIZE_MAX / 2) {
                next = needed;
                break;
            }
            next *= 2;
        }
        char *grown = realloc(compiled->literals, next);
        if (grown == NULL) {
            return false;
        }
        compiled->literals = grown;
        *capacity = next;
    }
    compiled->literals[compiled->literals_length++] = sanitize_char(ch);
    return true;
}

static bool template_add_chunk(photoc_filename_template *compiled,
                               size_t *capacity, template_chunk chunk)
{
    if (compiled->count == *capacity) {
        size_t next = *capacity == 0 ? 8 : *capacity * 2;
        if (next < *capacity || next > SIZE_MAX / sizeof(*compiled->chunks)) {
            return false;
        }
        template_chunk *grown =
            realloc(compiled->chunks, next * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        compiled->chunks = grown;
        *capacity = next;
    }
    compiled->chunks[compiled->count++] = chunk;
    return true;
}

void photoc_filename_template_free(photoc_filename_template *compiled)
{
    if (compiled == NULL) {
        return;
    }
    free(compiled->literals);
    free(compiled->chunks);
    free(compiled);
}

photoc_template_result photoc_filename_template_compile(
    const char *pattern, photoc_filename_template **out, size_t *error_offset)
{
    if (error_offset != NULL) {
        *error_offset = SIZE_MAX;
    }
    if (out == NULL) {
        return PHOTOC_TEMPLATE_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (pattern == NULL) {
        return PHOTOC_TEMPLATE_INVALID_ARGUMENT;
    }
    if (pattern[0] == '\0') {
        return PHOTOC_TEMPLATE_INVALID_TEMPLATE;
    }
    if (strcmp(pattern, ".") == 0 || strcmp(pattern, "..") == 0) {
        return PHOTOC_TEMPLATE_INVALID_TEMPLATE;
    }

    photoc_filename_template *compiled = calloc(1, sizeof(*compiled));
    if (compiled == NULL) {
        return PHOTOC_TEMPLATE_NO_MEMORY;
    }
    size_t literal_capacity = 0;
    size_t chunk_capacity = 0;
    size_t run_start = 0;
    photoc_template_result result = PHOTOC_TEMPLATE_OK;

    for (size_t i = 0; pattern[i] != '\0';) {
        if (pattern[i] == '{' && pattern[i + 1] == '{') {
            if (!template_append_literal(compiled, &literal_capacity, '{')) {
                result = PHOTOC_TEMPLATE_NO_MEMORY;
                goto fail;
            }
            i += 2;
        } else if (pattern[i] == '}' && pattern[i + 1] == '}') {
            if (!template_append_literal(compiled, &literal_capacity, '}')) {
                result = PHOTOC_TEMPLATE_NO_MEMORY;
                goto fail;
            }
            i += 2;
        } else if (pattern[i] == '{') {
            size_t start = i++;
            size_t token_start = i;
            while (pattern[i] != '\0' && pattern[i] != '}' &&
                   pattern[i] != '{') {
                ++i;
            }
            if (error_offset != NULL) {
                *error_offset = start;
            }
            if (pattern[i] != '}' || i == token_start) {
                result = PHOTOC_TEMPLATE_INVALID_TEMPLATE;
                goto fail;
            }
            template_token token;
            if (!token_kind(pattern + token_start, i - token_start, &token)) {
                result = PHOTOC_TEMPLATE_UNKNOWN_PLACEHOLDER;
                goto fail;
            }
            if (compiled->literals_length > run_start) {
                if (!template_add_chunk(
                        compiled, &chunk_capacity,
                        (template_chunk){.is_token = false,
                                         .literal_start = run_start,
                                         .literal_length =
                                             compiled->literals_length -
                                             run_start})) {
                    result = PHOTOC_TEMPLATE_NO_MEMORY;
                    goto fail;
                }
                run_start = compiled->literals_length;
            }
            if (!template_add_chunk(compiled, &chunk_capacity,
                                    (template_chunk){.is_token = true,
                                                     .token = token,
                                                     .offset = start})) {
                result = PHOTOC_TEMPLATE_NO_MEMORY;
                goto fail;
            }
            if (error_offset != NULL) {
                *error_offset = SIZE_MAX;
            }
            ++i;
        } else if (pattern[i] == '}') {
            if (error_offset != NULL) {
                *error_offset = i;
            }
            result = PHOTOC_TEMPLATE_INVALID_TEMPLATE;
            goto fail;
        } else {
            if (!template_append_literal(compiled, &literal_capacity,
                                         pattern[i++])) {
                result = PHOTOC_TEMPLATE_NO_MEMORY;
                goto fail;
            }
        }
    }
    if (compiled->literals_length > run_start) {
        if (!template_add_chunk(
                compiled, &chunk_capacity,
                (template_chunk){.is_token = false,
                                 .literal_start = run_start,
                                 .literal_length =
                                     compiled->literals_length - run_start})) {
            result = PHOTOC_TEMPLATE_NO_MEMORY;
            goto fail;
        }
    }
    *out = compiled;
    return PHOTOC_TEMPLATE_OK;

fail:
    photoc_filename_template_free(compiled);
    return result;
}

static photoc_template_result
resolve_token(template_token token, const Photo *photo, const char *original,
              size_t original_length, const char *extension, uint64_t sequence,
              unsigned int sequence_width, char formatted[64],
              const char **value, size_t *length)
{
    *value = NULL;
    *length = 0;
    switch (token) {
    case TEMPLATE_TOKEN_DATE:
    case TEMPLATE_TOKEN_DATETIME: {
        if (!photoc_timestamp_is_valid(photo->capture_timestamp)) {
            return PHOTOC_TEMPLATE_MISSING_VALUE;
        }
        bool date_only = token == TEMPLATE_TOKEN_DATE;
        size_t value_length = date_only ? 10 : 19;
        memcpy(formatted, photo->capture_timestamp, value_length);
        formatted[4] = '-';
        formatted[7] = '-';
        if (!date_only) {
            formatted[10] = '_';
            formatted[13] = '-';
            formatted[16] = '-';
        }
        *value = formatted;
        *length = value_length;
        return PHOTOC_TEMPLATE_OK;
    }
    case TEMPLATE_TOKEN_CAMERA:
        *value = photo->camera_model;
        break;
    case TEMPLATE_TOKEN_MAKE:
        *value = photo->camera_make;
        break;
    case TEMPLATE_TOKEN_ISO: {
        if (!photo->has_iso || photo->iso == 0) {
            return PHOTOC_TEMPLATE_MISSING_VALUE;
        }
        int written = snprintf(formatted, 64, "%" PRIu32, photo->iso);
        if (written < 0 || written >= 64) {
            return PHOTOC_TEMPLATE_TOO_LONG;
        }
        *value = formatted;
        *length = (size_t)written;
        return PHOTOC_TEMPLATE_OK;
    }
    case TEMPLATE_TOKEN_APERTURE:
    case TEMPLATE_TOKEN_FOCAL: {
        bool has_value = token == TEMPLATE_TOKEN_APERTURE
                             ? photo->has_aperture
                             : photo->has_focal_length;
        double number = token == TEMPLATE_TOKEN_APERTURE ? photo->aperture
                                                         : photo->focal_length;
        if (!has_value || !isfinite(number) || number <= 0.0) {
            return PHOTOC_TEMPLATE_MISSING_VALUE;
        }
        int written = snprintf(formatted, 64, "%.15g", number);
        if (written < 0 || written >= 64) {
            return PHOTOC_TEMPLATE_TOO_LONG;
        }
        *value = formatted;
        *length = (size_t)written;
        return PHOTOC_TEMPLATE_OK;
    }
    case TEMPLATE_TOKEN_SEQUENCE: {
        int written = snprintf(formatted, 64, "%0*" PRIu64, (int)sequence_width,
                               sequence);
        if (written < 0 || written >= 64) {
            return PHOTOC_TEMPLATE_TOO_LONG;
        }
        *value = formatted;
        *length = (size_t)written;
        return PHOTOC_TEMPLATE_OK;
    }
    case TEMPLATE_TOKEN_ORIGINAL:
        *value = original;
        *length = original_length;
        break;
    case TEMPLATE_TOKEN_EXT:
        *value = extension;
        break;
    }
    if (*value == NULL || (*length == 0 && (*value)[0] == '\0')) {
        return PHOTOC_TEMPLATE_MISSING_VALUE;
    }
    if (*length == 0) {
        *length = strlen(*value);
    }
    return PHOTOC_TEMPLATE_OK;
}

photoc_template_result
photoc_filename_template_render(const photoc_filename_template *compiled,
                                const Photo *photo, uint64_t sequence,
                                unsigned int sequence_width, char **filename,
                                size_t *error_offset)
{
    if (error_offset != NULL) {
        *error_offset = SIZE_MAX;
    }
    if (filename == NULL) {
        return PHOTOC_TEMPLATE_INVALID_ARGUMENT;
    }
    *filename = NULL;
    if (compiled == NULL || photo == NULL || photo->path == NULL ||
        photo->path[0] == '\0' || sequence_width > 20) {
        return PHOTOC_TEMPLATE_INVALID_ARGUMENT;
    }

    char *basename = NULL;
    char *extension = NULL;
    if (photoc_fs_filename(photo->path, &basename) != 0 ||
        photoc_fs_extension(photo->path, &extension) != 0) {
        photoc_template_result result = errno == ENOMEM
                                            ? PHOTOC_TEMPLATE_NO_MEMORY
                                            : PHOTOC_TEMPLATE_INVALID_ARGUMENT;
        free(basename);
        free(extension);
        return result;
    }
    size_t original_length = strlen(basename);
    if (extension != NULL) {
        size_t extension_length = strlen(extension);
        if (original_length < extension_length + 1) {
            free(basename);
            free(extension);
            return PHOTOC_TEMPLATE_INVALID_ARGUMENT;
        }
        original_length -= extension_length + 1;
    }

    /* First find the output size, then write once into that buffer. Literal
       chunks are already sanitized; token values are sanitized while copied. */
    char formatted[64];
    size_t total = 0;
    for (size_t i = 0; i < compiled->count; ++i) {
        const template_chunk *chunk = &compiled->chunks[i];
        if (!chunk->is_token) {
            if (chunk->literal_length > SIZE_MAX - total) {
                free(basename);
                free(extension);
                return PHOTOC_TEMPLATE_TOO_LONG;
            }
            total += chunk->literal_length;
            continue;
        }
        const char *value = NULL;
        size_t length = 0;
        photoc_template_result result = resolve_token(
            chunk->token, photo, basename, original_length, extension, sequence,
            sequence_width, formatted, &value, &length);
        if (result != PHOTOC_TEMPLATE_OK) {
            if (result == PHOTOC_TEMPLATE_MISSING_VALUE &&
                error_offset != NULL) {
                *error_offset = chunk->offset;
            }
            free(basename);
            free(extension);
            return result;
        }
        if (length > SIZE_MAX - total) {
            free(basename);
            free(extension);
            return PHOTOC_TEMPLATE_TOO_LONG;
        }
        total += length;
    }
    char *output = malloc(total + 1);
    if (output == NULL) {
        free(basename);
        free(extension);
        return PHOTOC_TEMPLATE_NO_MEMORY;
    }
    size_t at = 0;
    for (size_t i = 0; i < compiled->count; ++i) {
        const template_chunk *chunk = &compiled->chunks[i];
        if (!chunk->is_token) {
            memcpy(output + at, compiled->literals + chunk->literal_start,
                   chunk->literal_length);
            at += chunk->literal_length;
            continue;
        }
        const char *value = NULL;
        size_t length = 0;
        (void)resolve_token(chunk->token, photo, basename, original_length,
                            extension, sequence, sequence_width, formatted,
                            &value, &length);
        for (size_t j = 0; j < length; ++j) {
            output[at + j] = sanitize_char(value[j]);
        }
        at += length;
    }
    output[at] = '\0';
    if (at == 0 || strcmp(output, ".") == 0 || strcmp(output, "..") == 0) {
        free(output);
        free(basename);
        free(extension);
        return PHOTOC_TEMPLATE_INVALID_TEMPLATE;
    }
    free(basename);
    free(extension);
    *filename = output;
    return PHOTOC_TEMPLATE_OK;
}

photoc_template_result
photoc_filename_template_expand(const char *pattern, const Photo *photo,
                                uint64_t sequence, unsigned int sequence_width,
                                char **filename, size_t *error_offset)
{
    if (error_offset != NULL) {
        *error_offset = SIZE_MAX;
    }
    if (filename == NULL) {
        return PHOTOC_TEMPLATE_INVALID_ARGUMENT;
    }
    *filename = NULL;
    if (pattern == NULL || photo == NULL || photo->path == NULL ||
        photo->path[0] == '\0' || sequence_width > 20) {
        return PHOTOC_TEMPLATE_INVALID_ARGUMENT;
    }
    photoc_filename_template *compiled = NULL;
    photoc_template_result result =
        photoc_filename_template_compile(pattern, &compiled, error_offset);
    if (result == PHOTOC_TEMPLATE_OK) {
        result = photoc_filename_template_render(
            compiled, photo, sequence, sequence_width, filename, error_offset);
    }
    photoc_filename_template_free(compiled);
    return result;
}

const char *photoc_template_result_message(photoc_template_result result)
{
    switch (result) {
    case PHOTOC_TEMPLATE_OK:
        return "success";
    case PHOTOC_TEMPLATE_INVALID_ARGUMENT:
        return "invalid template argument";
    case PHOTOC_TEMPLATE_INVALID_TEMPLATE:
        return "invalid filename template";
    case PHOTOC_TEMPLATE_UNKNOWN_PLACEHOLDER:
        return "unknown placeholder";
    case PHOTOC_TEMPLATE_MISSING_VALUE:
        return "placeholder value is unavailable or invalid";
    case PHOTOC_TEMPLATE_NO_MEMORY:
        return "out of memory";
    case PHOTOC_TEMPLATE_TOO_LONG:
        return "filename is too long";
    }
    return "unknown template error";
}
