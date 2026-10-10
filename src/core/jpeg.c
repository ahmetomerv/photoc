#define _POSIX_C_SOURCE 200809L

#include "jpeg.h"

#include <errno.h>
#include <string.h>

static int invalid_or_io(FILE *file)
{
    if (ferror(file)) {
        if (errno == 0) {
            errno = EIO;
        }
    } else {
        errno = EINVAL;
    }
    return -1;
}

static int header_marker(FILE *file, uint64_t limit)
{
    off_t offset = ftello(file);
    if (offset < 0 || (uint64_t)offset >= limit)
        return -1;
    uint64_t position = (uint64_t)offset;
    int value = fgetc(file);
    if (value != 0xff) {
        return -1;
    }
    do {
        if (++position >= limit)
            return -1;
        value = fgetc(file);
    } while (value == 0xff);
    return value == 0x00 ? -1 : value;
}

enum { SCAN_BUFFER_SIZE = 16384 };

static int scan_marker(FILE *file, uint64_t limit)
{
    off_t offset = ftello(file);
    if (offset < 0) {
        return -1;
    }
    uint64_t position = (uint64_t)offset;
    unsigned char buffer[SCAN_BUFFER_SIZE];
    bool pending_ff = false;
    while (position < limit) {
        uint64_t remaining = limit - position;
        size_t want =
            remaining < sizeof(buffer) ? (size_t)remaining : sizeof(buffer);
        size_t got = fread(buffer, 1, want, file);
        if (got == 0) {
            return -1;
        }
        size_t index = 0;
        while (index < got) {
            if (!pending_ff) {
                const unsigned char *found =
                    memchr(buffer + index, 0xff, got - index);
                if (found == NULL) {
                    break;
                }
                index = (size_t)(found - buffer) + 1;
                pending_ff = true;
                continue;
            }
            unsigned char value = buffer[index++];
            if (value == 0xff) {
                continue;
            }
            if (value == 0x00 || (value >= 0xd0 && value <= 0xd7)) {
                pending_ff = false;
                continue;
            }
            if (fseeko(file, (off_t)(position + index), SEEK_SET) != 0) {
                return -1;
            }
            return (int)value;
        }
        position += got;
        if (got < want) {
            return -1;
        }
    }
    return -1;
}

static int skip_bytes(FILE *file, unsigned int count)
{
    unsigned char buffer[1024];
    while (count != 0) {
        size_t chunk = count < sizeof(buffer) ? count : sizeof(buffer);
        if (fread(buffer, 1, chunk, file) != chunk) {
            return -1;
        }
        count -= (unsigned int)chunk;
    }
    return 0;
}

static bool is_frame_marker(int value)
{
    return (value >= 0xc0 && value <= 0xc3) ||
           (value >= 0xc5 && value <= 0xc7) ||
           (value >= 0xc9 && value <= 0xcb) || (value >= 0xcd && value <= 0xcf);
}

int photoc_jpeg_inspect_app_limited(FILE *file, photoc_jpeg_info *info,
                                    photoc_jpeg_app_visitor visitor,
                                    void *user_data, uint64_t byte_limit)
{
    if (file == NULL || info == NULL) {
        errno = EINVAL;
        return -1;
    }
    *info = (photoc_jpeg_info){0};
    info->exif_insert_offset = 2;
    if (byte_limit < 2 || fgetc(file) != 0xff || fgetc(file) != 0xd8) {
        return invalid_or_io(file);
    }

    bool has_frame = false;
    bool has_scan = false;
    bool leading_app0 = true;
    int current = header_marker(file, byte_limit);
    while (current >= 0) {
        if (current == 0xd9) {
            return has_frame && has_scan ? 0 : invalid_or_io(file);
        }
        if (current == 0xd8 || (current >= 0xd0 && current <= 0xd7)) {
            return invalid_or_io(file);
        }
        if (current == 0x01) {
            current = header_marker(file, byte_limit);
            continue;
        }

        off_t marker_end = ftello(file);
        if (marker_end < 2) {
            return -1;
        }
        if ((uint64_t)marker_end > byte_limit ||
            byte_limit - (uint64_t)marker_end < 2)
            return invalid_or_io(file);
        uint64_t marker_offset = (uint64_t)(marker_end - 2);
        int high = fgetc(file);
        int low = fgetc(file);
        if (high == EOF || low == EOF) {
            return invalid_or_io(file);
        }
        unsigned int length = ((unsigned int)high << 8) | (unsigned int)low;
        if (length < 2 || length > byte_limit - (uint64_t)marker_end) {
            return invalid_or_io(file);
        }
        if (leading_app0 && current == 0xe0) {
            info->exif_insert_offset = marker_offset + (uint64_t)length + 2;
        } else {
            leading_app0 = false;
        }

        if (is_frame_marker(current)) {
            if (length < 11 || has_frame) {
                return invalid_or_io(file);
            }
            int precision = fgetc(file);
            int h1 = fgetc(file);
            int h2 = fgetc(file);
            int w1 = fgetc(file);
            int w2 = fgetc(file);
            int components = fgetc(file);
            if (precision <= 0 || h1 == EOF || h2 == EOF || w1 == EOF ||
                w2 == EOF || components <= 0 ||
                length != 8u + 3u * (unsigned int)components) {
                return invalid_or_io(file);
            }
            info->components = (unsigned int)components;
            info->height = (uint32_t)((h1 << 8) | h2);
            info->width = (uint32_t)((w1 << 8) | w2);
            if (info->height == 0 || info->width == 0 ||
                skip_bytes(file, length - 8) != 0) {
                return invalid_or_io(file);
            }
            has_frame = true;
        } else if ((current >= 0xe0 && current <= 0xef) || current == 0xfe) {
            unsigned char payload[65533];
            unsigned int payload_length = length - 2;
            if (fread(payload, 1, payload_length, file) != payload_length) {
                return invalid_or_io(file);
            }
            if (current == 0xe1 && payload_length >= 6 &&
                memcmp(payload, "Exif\0\0", 6) == 0) {
                if (info->exif_count == 0) {
                    info->exif_offset = marker_offset;
                    info->exif_length = (uint64_t)length + 2;
                }
                ++info->exif_count;
                if (has_scan) {
                    info->exif_after_scan = true;
                }
            }
            if (visitor != NULL &&
                visitor((unsigned char)current, payload, payload_length,
                        has_scan, user_data) != 0) {
                return -1;
            }
        } else {
            if (current == 0xda && (!has_frame || length < 6)) {
                return invalid_or_io(file);
            }
            if (skip_bytes(file, length - 2) != 0) {
                return invalid_or_io(file);
            }
        }

        if (current == 0xda) {
            ++info->scan_count;
            if (!has_scan) {
                info->scan_offset = marker_offset;
            }
            has_scan = true;
            current = scan_marker(file, byte_limit);
        } else {
            current = header_marker(file, byte_limit);
        }
    }
    return invalid_or_io(file);
}

int photoc_jpeg_inspect_app(FILE *file, photoc_jpeg_info *info,
                            photoc_jpeg_app_visitor visitor, void *user_data)
{
    return photoc_jpeg_inspect_app_limited(file, info, visitor, user_data,
                                           UINT64_MAX);
}

int photoc_jpeg_inspect(FILE *file, photoc_jpeg_info *info)
{
    return photoc_jpeg_inspect_app(file, info, NULL, NULL);
}
