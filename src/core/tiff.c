#define _POSIX_C_SOURCE 200809L

#include "tiff.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>

typedef struct {
    uint32_t offset;
    photoc_tiff_directory_kind kind;
    bool primary;
} tiff_directory;

static bool invalid(void)
{
    errno = EINVAL;
    return false;
}

bool photoc_tiff_read(photoc_tiff_reader *reader, uint64_t offset,
                      unsigned char *destination, size_t size)
{
    if (offset > reader->size || size > reader->size - offset)
        return invalid();
    if (size > PHOTOC_TIFF_READ_BUDGET - reader->bytes_read) {
        errno = EFBIG;
        return false;
    }
    /* Classic TIFF offsets and file sizes are at most UINT32_MAX. */
    if (fseeko(reader->file, (off_t)offset, SEEK_SET) != 0)
        return false;
    if (fread(destination, 1, size, reader->file) != size) {
        if (!ferror(reader->file))
            errno = EINVAL;
        else if (errno == 0)
            errno = EIO;
        return false;
    }
    reader->bytes_read += size;
    return true;
}

static bool enqueue(tiff_directory *directories, size_t *count, uint32_t offset,
                    photoc_tiff_directory_kind kind, bool primary)
{
    if (offset == 0)
        return true;
    if (offset < 8)
        return invalid();
    /* Repeated pointers and cycles are rejected rather than revisited. */
    for (size_t i = 0; i < *count; ++i) {
        if (directories[i].offset == offset)
            return invalid();
    }
    if (*count == PHOTOC_TIFF_MAX_DIRECTORIES) {
        errno = EFBIG;
        return false;
    }
    directories[(*count)++] = (tiff_directory){offset, kind, primary};
    return true;
}

static unsigned int type_size(uint16_t type)
{
    static const unsigned int sizes[] = {0, 1, 1, 2, 4, 8, 1,
                                         1, 2, 4, 8, 4, 8, 4};
    return type < sizeof(sizes) / sizeof(sizes[0]) ? sizes[type] : 0;
}

static bool follow_links(photoc_tiff_reader *reader,
                         const photoc_tiff_entry *entry,
                         tiff_directory *directories, size_t *count)
{
    photoc_tiff_directory_kind kind;
    if (entry->kind == PHOTOC_TIFF_IMAGE && entry->tag == 0x014a)
        kind = PHOTOC_TIFF_IMAGE;
    else if (entry->kind == PHOTOC_TIFF_IMAGE && entry->tag == 0x8769)
        kind = PHOTOC_TIFF_EXIF;
    else if (entry->kind == PHOTOC_TIFF_IMAGE && entry->tag == 0x8825)
        kind = PHOTOC_TIFF_GPS;
    else if (entry->kind == PHOTOC_TIFF_EXIF && entry->tag == 0xa005)
        kind = PHOTOC_TIFF_INTEROP;
    else
        return true;
    if ((entry->type != 4 && entry->type != 13) || entry->count == 0 ||
        (entry->tag != 0x014a && entry->count != 1))
        return invalid();
    if (entry->count > PHOTOC_TIFF_MAX_DIRECTORIES) {
        errno = EFBIG;
        return false;
    }
    for (uint32_t i = 0; i < entry->count; ++i) {
        unsigned char bytes[4];
        if (!photoc_tiff_read(reader, entry->offset + (uint64_t)i * 4, bytes,
                              4) ||
            !enqueue(directories, count, exif_get_long(bytes, reader->order),
                     kind, false))
            return false;
    }
    return true;
}

int photoc_tiff_walk(FILE *file, uint64_t size, photoc_tiff_visit_fn visit,
                     void *data)
{
    if (file == NULL || visit == NULL || size > UINT32_MAX) {
        errno = size > UINT32_MAX ? EFBIG : EINVAL;
        return -1;
    }
    photoc_tiff_reader reader = {.file = file, .size = size};
    unsigned char header[8];
    if (!photoc_tiff_read(&reader, 0, header, sizeof(header)))
        return -1;
    if (memcmp(header, "II", 2) == 0)
        reader.order = EXIF_BYTE_ORDER_INTEL;
    else if (memcmp(header, "MM", 2) == 0)
        reader.order = EXIF_BYTE_ORDER_MOTOROLA;
    else {
        errno = EINVAL;
        return -1;
    }
    if (exif_get_short(header + 2, reader.order) == 43) {
        errno = ENOTSUP;
        return -1;
    }
    if (exif_get_short(header + 2, reader.order) != 42) {
        errno = EINVAL;
        return -1;
    }
    tiff_directory directories[PHOTOC_TIFF_MAX_DIRECTORIES];
    size_t count = 0;
    uint32_t first = exif_get_long(header + 4, reader.order);
    if (first == 0 ||
        !enqueue(directories, &count, first, PHOTOC_TIFF_IMAGE, true)) {
        errno = EINVAL;
        return -1;
    }
    uint32_t total_entries = 0;
    for (size_t i = 0; i < count; ++i) {
        tiff_directory directory = directories[i];
        unsigned char bytes[12];
        if (!photoc_tiff_read(&reader, directory.offset, bytes, 2))
            return -1;
        uint16_t entries = exif_get_short(bytes, reader.order);
        if (entries > PHOTOC_TIFF_MAX_ENTRIES - total_entries) {
            errno = EFBIG;
            return -1;
        }
        total_entries += entries;
        uint64_t start = (uint64_t)directory.offset + 2;
        uint64_t table_size = (uint64_t)entries * 12 + 4;
        if (start > size || table_size > size - start) {
            errno = EINVAL;
            return -1;
        }
        uint16_t previous = 0;
        for (uint16_t j = 0; j < entries; ++j) {
            uint64_t offset = start + (uint64_t)j * 12;
            if (!photoc_tiff_read(&reader, offset, bytes, 12))
                return -1;
            photoc_tiff_entry entry = {
                .tag = exif_get_short(bytes, reader.order),
                .type = exif_get_short(bytes + 2, reader.order),
                .count = exif_get_long(bytes + 4, reader.order),
                .directory_offset = directory.offset,
                .kind = directory.kind,
                .primary = directory.primary};
            unsigned int unit = type_size(entry.type);
            /* TIFF requires unique tags in ascending order within an IFD. */
            if (unit == 0 || (j != 0 && entry.tag <= previous)) {
                errno = EINVAL;
                return -1;
            }
            previous = entry.tag;
            entry.size = (uint64_t)unit * entry.count;
            entry.offset = entry.size <= 4
                               ? offset + 8
                               : exif_get_long(bytes + 8, reader.order);
            if (entry.offset > size || entry.size > size - entry.offset ||
                (entry.size > 4 && entry.offset < 8)) {
                errno = EINVAL;
                return -1;
            }
            if (!follow_links(&reader, &entry, directories, &count) ||
                !visit(&reader, &entry, data))
                return -1;
        }
        if (!photoc_tiff_read(&reader, start + (uint64_t)entries * 12, bytes,
                              4) ||
            !enqueue(directories, &count, exif_get_long(bytes, reader.order),
                     directory.kind, false))
            return -1;
    }
    return 0;
}
