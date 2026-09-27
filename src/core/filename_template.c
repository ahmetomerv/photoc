#include "photoc/filename_template.h"

#include "photoc/fs.h"
#include "photoc/timestamp.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} filename_buffer;

static photoc_template_result append_char(filename_buffer *buffer, char ch)
{
    if (buffer->length == SIZE_MAX - 1) {
        return PHOTOC_TEMPLATE_TOO_LONG;
    }
    size_t needed = buffer->length + 2;
    if (needed > buffer->capacity) {
        size_t capacity = buffer->capacity == 0 ? 64 : buffer->capacity;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) {
                capacity = needed;
                break;
            }
            capacity *= 2;
        }
        char *data = realloc(buffer->data, capacity);
        if (data == NULL) {
            return PHOTOC_TEMPLATE_NO_MEMORY;
        }
        buffer->data = data;
        buffer->capacity = capacity;
    }
    unsigned char byte = (unsigned char)ch;
    if (byte < 0x20 || byte == 0x7f || ch == '/' || ch == '\\' ||
        ch == ':' || ch == '*' || ch == '?' || ch == '"' ||
        ch == '<' || ch == '>' || ch == '|') {
        ch = '_';
    }
    buffer->data[buffer->length++] = ch;
    buffer->data[buffer->length] = '\0';
    return PHOTOC_TEMPLATE_OK;
}

static photoc_template_result append_text(filename_buffer *buffer,
                                          const char *text, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        photoc_template_result result = append_char(buffer, text[i]);
        if (result != PHOTOC_TEMPLATE_OK) {
            return result;
        }
    }
    return PHOTOC_TEMPLATE_OK;
}

static bool token_is(const char *token, size_t length, const char *name)
{
    return strlen(name) == length && memcmp(token, name, length) == 0;
}

static photoc_template_result format_number(char *buffer, size_t size,
                                             const char *format, double value)
{
    int length = snprintf(buffer, size, format, value);
    return length < 0 || (size_t)length >= size ?
        PHOTOC_TEMPLATE_TOO_LONG : PHOTOC_TEMPLATE_OK;
}

static photoc_template_result expand_token(filename_buffer *buffer,
                                           const char *token, size_t length,
                                           const Photo *photo,
                                           const char *original,
                                           size_t original_length,
                                           const char *extension,
                                           uint64_t sequence,
                                           unsigned int sequence_width)
{
    const char *value = NULL;
    size_t value_length = 0;
    char formatted[64];

    if (token_is(token, length, "date") ||
        token_is(token, length, "datetime")) {
        if (!photoc_timestamp_is_valid(photo->capture_timestamp)) {
            return PHOTOC_TEMPLATE_MISSING_VALUE;
        }
        bool date_only = token_is(token, length, "date");
        value_length = date_only ? 10 : 19;
        memcpy(formatted, photo->capture_timestamp, value_length);
        formatted[4] = '-';
        formatted[7] = '-';
        if (!date_only) {
            formatted[10] = '_';
            formatted[13] = '-';
            formatted[16] = '-';
        }
        value = formatted;
    } else if (token_is(token, length, "camera")) {
        value = photo->camera_model;
    } else if (token_is(token, length, "make")) {
        value = photo->camera_make;
    } else if (token_is(token, length, "iso")) {
        if (!photo->has_iso || photo->iso == 0) {
            return PHOTOC_TEMPLATE_MISSING_VALUE;
        }
        int written = snprintf(formatted, sizeof(formatted), "%" PRIu32,
                               photo->iso);
        if (written < 0 || (size_t)written >= sizeof(formatted)) {
            return PHOTOC_TEMPLATE_TOO_LONG;
        }
        value = formatted;
    } else if (token_is(token, length, "aperture")) {
        if (!photo->has_aperture || !isfinite(photo->aperture) ||
            photo->aperture <= 0.0) {
            return PHOTOC_TEMPLATE_MISSING_VALUE;
        }
        photoc_template_result result = format_number(
            formatted, sizeof(formatted), "%.15g", photo->aperture);
        if (result != PHOTOC_TEMPLATE_OK) {
            return result;
        }
        value = formatted;
    } else if (token_is(token, length, "focal")) {
        if (!photo->has_focal_length || !isfinite(photo->focal_length) ||
            photo->focal_length <= 0.0) {
            return PHOTOC_TEMPLATE_MISSING_VALUE;
        }
        photoc_template_result result = format_number(
            formatted, sizeof(formatted), "%.15g", photo->focal_length);
        if (result != PHOTOC_TEMPLATE_OK) {
            return result;
        }
        value = formatted;
    } else if (token_is(token, length, "sequence")) {
        int written = snprintf(formatted, sizeof(formatted), "%0*" PRIu64,
                               (int)sequence_width, sequence);
        if (written < 0 || (size_t)written >= sizeof(formatted)) {
            return PHOTOC_TEMPLATE_TOO_LONG;
        }
        value = formatted;
    } else if (token_is(token, length, "original")) {
        value = original;
        value_length = original_length;
    } else if (token_is(token, length, "ext")) {
        value = extension;
    } else {
        return PHOTOC_TEMPLATE_UNKNOWN_PLACEHOLDER;
    }

    if (value == NULL || (value_length == 0 && value[0] == '\0')) {
        return PHOTOC_TEMPLATE_MISSING_VALUE;
    }
    if (value_length == 0) {
        value_length = strlen(value);
    }
    return append_text(buffer, value, value_length);
}

photoc_template_result photoc_filename_template_expand(
    const char *pattern, const Photo *photo, uint64_t sequence,
    unsigned int sequence_width, char **filename, size_t *error_offset)
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
    if (pattern[0] == '\0') {
        return PHOTOC_TEMPLATE_INVALID_TEMPLATE;
    }

    char *basename = NULL;
    char *extension = NULL;
    filename_buffer buffer = {0};
    photoc_template_result result = PHOTOC_TEMPLATE_OK;
    if (photoc_fs_filename(photo->path, &basename) != 0 ||
        photoc_fs_extension(photo->path, &extension) != 0) {
        result = errno == ENOMEM ? PHOTOC_TEMPLATE_NO_MEMORY :
                 PHOTOC_TEMPLATE_INVALID_ARGUMENT;
        goto done;
    }
    size_t original_length = strlen(basename);
    if (extension != NULL) {
        original_length -= strlen(extension) + 1;
    }

    for (size_t i = 0; pattern[i] != '\0';) {
        if (pattern[i] == '{' && pattern[i + 1] == '{') {
            result = append_char(&buffer, '{');
            i += 2;
        } else if (pattern[i] == '}' && pattern[i + 1] == '}') {
            result = append_char(&buffer, '}');
            i += 2;
        } else if (pattern[i] == '{') {
            size_t start = i++;
            size_t token_start = i;
            while (pattern[i] != '\0' && pattern[i] != '}' &&
                   pattern[i] != '{') {
                ++i;
            }
            if (pattern[i] != '}' || i == token_start) {
                result = PHOTOC_TEMPLATE_INVALID_TEMPLATE;
            } else {
                result = expand_token(&buffer, pattern + token_start,
                                      i - token_start, photo, basename,
                                      original_length, extension, sequence,
                                      sequence_width);
                ++i;
            }
            if (result == PHOTOC_TEMPLATE_INVALID_TEMPLATE ||
                result == PHOTOC_TEMPLATE_UNKNOWN_PLACEHOLDER ||
                result == PHOTOC_TEMPLATE_MISSING_VALUE) {
                if (error_offset != NULL) {
                    *error_offset = start;
                }
            }
        } else if (pattern[i] == '}') {
            result = PHOTOC_TEMPLATE_INVALID_TEMPLATE;
            if (error_offset != NULL) {
                *error_offset = i;
            }
        } else {
            result = append_char(&buffer, pattern[i++]);
        }
        if (result != PHOTOC_TEMPLATE_OK) {
            goto done;
        }
    }
    if (buffer.length == 0 || strcmp(buffer.data, ".") == 0 ||
        strcmp(buffer.data, "..") == 0) {
        result = PHOTOC_TEMPLATE_INVALID_TEMPLATE;
        goto done;
    }
    *filename = buffer.data;
    buffer.data = NULL;

done:
    free(buffer.data);
    free(extension);
    free(basename);
    return result;
}

const char *photoc_template_result_message(photoc_template_result result)
{
    switch (result) {
    case PHOTOC_TEMPLATE_OK: return "success";
    case PHOTOC_TEMPLATE_INVALID_ARGUMENT: return "invalid template argument";
    case PHOTOC_TEMPLATE_INVALID_TEMPLATE: return "invalid filename template";
    case PHOTOC_TEMPLATE_UNKNOWN_PLACEHOLDER: return "unknown placeholder";
    case PHOTOC_TEMPLATE_MISSING_VALUE: return "placeholder value is unavailable or invalid";
    case PHOTOC_TEMPLATE_NO_MEMORY: return "out of memory";
    case PHOTOC_TEMPLATE_TOO_LONG: return "filename is too long";
    }
    return "unknown template error";
}
