#include "photoc/json.h"

#include <stddef.h>
#include <string.h>
#include <stdbool.h>

static size_t valid_utf8_length(const unsigned char *text, size_t remaining)
{
    unsigned char first = text[0];
    size_t length;
    unsigned int codepoint;
    unsigned int minimum;

    if (first >= 0xc2 && first <= 0xdf) {
        length = 2;
        codepoint = first & 0x1f;
        minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
        length = 3;
        codepoint = first & 0x0f;
        minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
        length = 4;
        codepoint = first & 0x07;
        minimum = 0x10000;
    } else {
        return 0;
    }

    if (remaining < length) {
        return 0;
    }
    for (size_t i = 1; i < length; ++i) {
        if (text[i] < 0x80 || text[i] > 0xbf) {
            return 0;
        }
        codepoint = (codepoint << 6) | (text[i] & 0x3f);
    }
    if (codepoint < minimum || codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
        return 0;
    }
    return length;
}

bool photoc_json_is_valid_utf8(const char *value)
{
    if (value == NULL)
        return false;
    const unsigned char *text = (const unsigned char *)value;
    size_t length = strlen(value);
    for (size_t i = 0; i < length;) {
        if (text[i] < 0x80) {
            ++i;
        } else {
            size_t sequence = valid_utf8_length(text + i, length - i);
            if (sequence == 0)
                return false;
            i += sequence;
        }
    }
    return true;
}

int photoc_json_write_string(FILE *stream, const char *value)
{
    if (stream == NULL) {
        return -1;
    }
    if (value == NULL) {
        return fputs("null", stream) == EOF ? -1 : 0;
    }
    if (fputc('"', stream) == EOF) {
        return -1;
    }

    const unsigned char *text = (const unsigned char *)value;
    size_t length = strlen(value);
    for (size_t i = 0; i < length;) {
        unsigned char ch = text[i];
        const char *escape = NULL;
        switch (ch) {
        case '"':
            escape = "\\\"";
            break;
        case '\\':
            escape = "\\\\";
            break;
        case '\b':
            escape = "\\b";
            break;
        case '\f':
            escape = "\\f";
            break;
        case '\n':
            escape = "\\n";
            break;
        case '\r':
            escape = "\\r";
            break;
        case '\t':
            escape = "\\t";
            break;
        default:
            break;
        }
        if (escape != NULL) {
            if (fputs(escape, stream) == EOF) {
                return -1;
            }
            ++i;
        } else if (ch < 0x20) {
            if (fprintf(stream, "\\u%04x", (unsigned int)ch) < 0) {
                return -1;
            }
            ++i;
        } else if (ch < 0x80) {
            if (fputc(ch, stream) == EOF) {
                return -1;
            }
            ++i;
        } else {
            size_t sequence = valid_utf8_length(text + i, length - i);
            if (sequence == 0) {
                if (fputs("\\ufffd", stream) == EOF) {
                    return -1;
                }
                ++i;
            } else {
                if (fwrite(text + i, 1, sequence, stream) != sequence) {
                    return -1;
                }
                i += sequence;
            }
        }
    }
    return fputc('"', stream) == EOF ? -1 : 0;
}
