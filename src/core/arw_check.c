#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/jpeg_check.h"

#include "check_internal.h"
#include "tiff.h"
#include "photoc/arw.h"
#include "photoc/fs.h"
#include "photoc/photo.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/* Strip/tile arrays beyond this are refused rather than read. */
#define ARW_CHECK_MAX_SEGMENTS 65536u

typedef struct {
    uint64_t offset; /* File position of the values. */
    uint32_t count;
    uint16_t type;
} value_ref;

typedef struct {
    uint32_t offset; /* Directory offset; entries arrive grouped by it. */
    uint32_t width;
    uint32_t height;
    uint32_t bits;
    uint32_t compression;
    uint32_t photometric;
    uint32_t subfile;
    value_ref segment_offsets; /* StripOffsets, or TileOffsets. */
    value_ref segment_counts;  /* StripByteCounts, or TileByteCounts. */
    uint32_t jpeg_offset;      /* Embedded preview/thumbnail, if any. */
    uint32_t jpeg_length;
    bool tiled;
} image_directory;

typedef struct {
    image_directory directories[PHOTOC_TIFF_MAX_DIRECTORIES];
    size_t count;
} image_layout;

static photoc_check_result finding(photoc_check_status status,
                                   photoc_check_code code, int system_errno)
{
    return (photoc_check_result){status, code, system_errno};
}

static photoc_check_result failure(photoc_check_code code)
{
    return finding(PHOTOC_CHECK_ERROR, code, 0);
}

static void remember_values(const photoc_tiff_entry *entry, value_ref *value)
{
    *value = (value_ref){entry->offset, entry->count, entry->type};
}

static bool collect_layout(photoc_tiff_reader *reader,
                           const photoc_tiff_entry *entry, void *data)
{
    image_layout *layout = data;
    if (entry->kind != PHOTOC_TIFF_IMAGE)
        return true;
    if (layout->count == 0 || layout->directories[layout->count - 1].offset !=
                                  entry->directory_offset) {
        if (layout->count == PHOTOC_TIFF_MAX_DIRECTORIES) {
            errno = EFBIG;
            return false;
        }
        layout->directories[layout->count++] =
            (image_directory){.offset = entry->directory_offset};
    }
    image_directory *directory = &layout->directories[layout->count - 1];
    switch (entry->tag) {
    case 0x00fe:
        return photoc_tiff_scalar(reader, entry, &directory->subfile);
    case 0x0100:
        return photoc_tiff_scalar(reader, entry, &directory->width);
    case 0x0101:
        return photoc_tiff_scalar(reader, entry, &directory->height);
    case 0x0102:
        /* One sample per CFA pixel; a SHORT array would repeat the value. */
        if (entry->type == 3 && entry->count >= 1) {
            unsigned char bytes[2];
            if (!photoc_tiff_read(reader, entry->offset, bytes, 2))
                return false;
            directory->bits = exif_get_short(bytes, reader->order);
        }
        return true;
    case 0x0103:
        return photoc_tiff_scalar(reader, entry, &directory->compression);
    case 0x0106:
        return photoc_tiff_scalar(reader, entry, &directory->photometric);
    case 0x0111:
        if (!directory->tiled)
            remember_values(entry, &directory->segment_offsets);
        return true;
    case 0x0117:
        if (!directory->tiled)
            remember_values(entry, &directory->segment_counts);
        return true;
    case 0x0144:
        directory->tiled = true;
        remember_values(entry, &directory->segment_offsets);
        return true;
    case 0x0145:
        directory->tiled = true;
        remember_values(entry, &directory->segment_counts);
        return true;
    case 0x0201:
        return photoc_tiff_scalar(reader, entry, &directory->jpeg_offset);
    case 0x0202:
        return photoc_tiff_scalar(reader, entry, &directory->jpeg_length);
    default:
        return true;
    }
}

/* The largest full-resolution CFA or linear RAW directory, as in metadata
   dimension selection. */
static const image_directory *sensor_directory(const image_layout *layout)
{
    const image_directory *best = NULL;
    for (size_t i = 0; i < layout->count; ++i) {
        const image_directory *directory = &layout->directories[i];
        if ((directory->photometric != 32803 &&
             directory->photometric != 34892) ||
            (directory->subfile & 1) != 0 || directory->width == 0 ||
            directory->height == 0)
            continue;
        if (best == NULL || (uint64_t)directory->width * directory->height >
                                (uint64_t)best->width * best->height)
            best = directory;
    }
    return best;
}

static bool read_value(FILE *file, ExifByteOrder order, const value_ref *ref,
                       uint32_t index, uint32_t *value)
{
    unsigned int unit = ref->type == 3 ? 2u : 4u;
    unsigned char bytes[4];
    if (fseeko(file, (off_t)(ref->offset + (uint64_t)index * unit), SEEK_SET) !=
            0 ||
        fread(bytes, 1, unit, file) != unit)
        return false;
    *value =
        unit == 2 ? exif_get_short(bytes, order) : exif_get_long(bytes, order);
    return true;
}

static photoc_check_result check_sensor(FILE *file, uint64_t size,
                                        ExifByteOrder order,
                                        const image_directory *sensor)
{
    const value_ref *offsets = &sensor->segment_offsets;
    const value_ref *counts = &sensor->segment_counts;
    if (offsets->count == 0 || counts->count == 0)
        return failure(PHOTOC_CHECK_MISSING_SENSOR_DATA);
    if (offsets->count != counts->count ||
        (offsets->type != 3 && offsets->type != 4) ||
        (counts->type != 3 && counts->type != 4))
        return failure(PHOTOC_CHECK_INVALID_ARW);
    if (offsets->count > ARW_CHECK_MAX_SEGMENTS)
        return failure(PHOTOC_CHECK_RESOURCE_LIMIT);

    uint64_t total = 0;
    bool lossless = sensor->compression == 7;
    for (uint32_t i = 0; i < offsets->count; ++i) {
        uint32_t offset = 0;
        uint32_t count = 0;
        if (!read_value(file, order, offsets, i, &offset) ||
            !read_value(file, order, counts, i, &count))
            return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_IO_ERROR,
                           errno == 0 ? EIO : errno);
        if (count == 0)
            return failure(PHOTOC_CHECK_MISSING_SENSOR_DATA);
        if (offset < 8 || offset > size || count > size - offset)
            return failure(PHOTOC_CHECK_TRUNCATED_SENSOR_DATA);
        if (lossless) {
            unsigned char marker[2];
            if (count < 2 || fseeko(file, (off_t)offset, SEEK_SET) != 0 ||
                fread(marker, 1, 2, file) != 2)
                return failure(PHOTOC_CHECK_INVALID_SENSOR_DATA);
            if (marker[0] != 0xff || marker[1] != 0xd8)
                return failure(PHOTOC_CHECK_INVALID_SENSOR_DATA);
        }
        total += count;
    }

    uint64_t pixels = (uint64_t)sensor->width * sensor->height;
    switch (sensor->compression) {
    case 1: {
        /* Uncompressed samples: packed, or one 16-bit word per sample. */
        uint64_t packed =
            (pixels * (sensor->bits == 0 ? 16 : sensor->bits) + 7) / 8;
        if (total < packed)
            return failure(PHOTOC_CHECK_TRUNCATED_SENSOR_DATA);
        if (total != packed && total != pixels * 2)
            return finding(PHOTOC_CHECK_WARNING,
                           PHOTOC_CHECK_SENSOR_LAYOUT_WARNING, 0);
        break;
    }
    case 32767:
        /* Sony compressed RAW stores one byte per pixel. */
        if (total < pixels)
            return failure(PHOTOC_CHECK_TRUNCATED_SENSOR_DATA);
        if (total != pixels)
            return finding(PHOTOC_CHECK_WARNING,
                           PHOTOC_CHECK_SENSOR_LAYOUT_WARNING, 0);
        break;
    case 7:
        break; /* Lossless tiles are variable length; markers were checked. */
    default:
        return finding(PHOTOC_CHECK_WARNING, PHOTOC_CHECK_SENSOR_LAYOUT_WARNING,
                       0);
    }
    return finding(PHOTOC_CHECK_OK, PHOTOC_CHECK_ARW_READABLE, 0);
}

static photoc_check_result check_metadata(const char *path)
{
    Photo photo = {0};
    photoc_metadata_result result = photo_load_metadata(path, &photo);
    int saved_errno = errno;
    photo_cleanup(&photo);
    switch (result) {
    case PHOTOC_METADATA_OK:
        return finding(PHOTOC_CHECK_OK, PHOTOC_CHECK_ARW_READABLE, 0);
    case PHOTOC_METADATA_UNSUPPORTED_FORMAT:
        return failure(PHOTOC_CHECK_UNSUPPORTED_ARW);
    case PHOTOC_METADATA_RESOURCE_LIMIT:
        return failure(PHOTOC_CHECK_RESOURCE_LIMIT);
    case PHOTOC_METADATA_IO_ERROR:
        return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_IO_ERROR, saved_errno);
    case PHOTOC_METADATA_NO_MEMORY:
        return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_INTERNAL_ERROR, ENOMEM);
    case PHOTOC_METADATA_INVALID_ARGUMENT:
    case PHOTOC_METADATA_INVALID_JPEG:
    case PHOTOC_METADATA_INVALID_ARW:
    case PHOTOC_METADATA_PRIVACY_REMAINS:
        break;
    }
    return failure(PHOTOC_CHECK_INVALID_ARW);
}

static photoc_check_result check_preview(const char *path)
{
    unsigned char *preview = NULL;
    size_t length = 0;
    photoc_image_result read =
        photoc_arw_read_preview(path, 0, &preview, &length);
    switch (read) {
    case PHOTOC_IMAGE_OK:
        break;
    case PHOTOC_IMAGE_NO_PREVIEW:
        return finding(PHOTOC_CHECK_WARNING, PHOTOC_CHECK_MISSING_PREVIEW, 0);
    case PHOTOC_IMAGE_IO_ERROR:
        return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_IO_ERROR,
                       errno == 0 ? EIO : errno);
    case PHOTOC_IMAGE_TOO_LARGE:
        return failure(PHOTOC_CHECK_RESOURCE_LIMIT);
    case PHOTOC_IMAGE_NO_MEMORY:
        return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_INTERNAL_ERROR, ENOMEM);
    case PHOTOC_IMAGE_INVALID_ARGUMENT:
    case PHOTOC_IMAGE_INVALID_JPEG:
    case PHOTOC_IMAGE_CODEC_ERROR:
    case PHOTOC_IMAGE_INVALID_ARW:
        return failure(PHOTOC_CHECK_INVALID_ARW);
    }
    FILE *stream = fmemopen(preview, length, "rb");
    if (stream == NULL) {
        int saved_errno = errno;
        free(preview);
        return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_INTERNAL_ERROR,
                       saved_errno);
    }
    photoc_check_result jpeg = photoc_jpeg_check_stream(stream, length);
    fclose(stream);
    free(preview);
    switch (jpeg.status) {
    case PHOTOC_CHECK_OK:
        return finding(PHOTOC_CHECK_OK, PHOTOC_CHECK_ARW_READABLE, 0);
    case PHOTOC_CHECK_WARNING:
        return finding(PHOTOC_CHECK_WARNING, PHOTOC_CHECK_PREVIEW_WARNING, 0);
    case PHOTOC_CHECK_ERROR:
        break;
    }
    if (jpeg.code == PHOTOC_CHECK_RESOURCE_LIMIT ||
        jpeg.code == PHOTOC_CHECK_INTERNAL_ERROR)
        return jpeg;
    return failure(PHOTOC_CHECK_PREVIEW_DECODE_FAILED);
}

photoc_check_result photoc_arw_check_file(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_INTERNAL_ERROR, EINVAL);
    FILE *file = NULL;
    uint64_t size = 0;
    if (photoc_fs_open_regular(path, &file, &size) != 0)
        return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_IO_ERROR, errno);
    struct stat before;
    if (fstat(fileno(file), &before) != 0) {
        int saved_errno = errno;
        fclose(file);
        return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_IO_ERROR, saved_errno);
    }

    photoc_check_result result =
        size == 0 ? failure(PHOTOC_CHECK_EMPTY_FILE) : check_metadata(path);
    image_layout *layout = NULL;
    if (result.status == PHOTOC_CHECK_OK) {
        layout = calloc(1, sizeof(*layout));
        if (layout == NULL)
            result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_INTERNAL_ERROR,
                             ENOMEM);
    }
    ExifByteOrder order = EXIF_BYTE_ORDER_INTEL;
    if (result.status == PHOTOC_CHECK_OK) {
        unsigned char header[2] = {0};
        errno = 0;
        if (fread(header, 1, 2, file) != 2 || fseeko(file, 0, SEEK_SET) != 0)
            result = failure(PHOTOC_CHECK_INVALID_ARW);
        order =
            header[0] == 'M' ? EXIF_BYTE_ORDER_MOTOROLA : EXIF_BYTE_ORDER_INTEL;
    }
    if (result.status == PHOTOC_CHECK_OK) {
        errno = 0;
        if (photoc_tiff_walk(file, size, collect_layout, layout) != 0)
            result = errno == EFBIG ? failure(PHOTOC_CHECK_RESOURCE_LIMIT)
                     : errno == EINVAL || errno == ENOTSUP
                         ? failure(PHOTOC_CHECK_INVALID_ARW)
                         : finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_IO_ERROR,
                                   errno == 0 ? EIO : errno);
    }
    if (result.status == PHOTOC_CHECK_OK) {
        const image_directory *sensor = sensor_directory(layout);
        result = sensor == NULL ? failure(PHOTOC_CHECK_MISSING_SENSOR_DATA)
                                : check_sensor(file, size, order, sensor);
    }
    /* Every embedded preview or thumbnail must lie within the file. */
    for (size_t i = 0; layout != NULL && result.status != PHOTOC_CHECK_ERROR &&
                       i < layout->count;
         ++i) {
        const image_directory *directory = &layout->directories[i];
        if (directory->jpeg_offset != 0 &&
            (uint64_t)directory->jpeg_offset + directory->jpeg_length > size)
            result = failure(PHOTOC_CHECK_INVALID_ARW);
    }
    free(layout);
    if (result.status != PHOTOC_CHECK_ERROR) {
        photoc_check_result preview = check_preview(path);
        if (preview.status == PHOTOC_CHECK_ERROR ||
            result.status == PHOTOC_CHECK_OK)
            result = preview;
    }
    struct stat after;
    if (fstat(fileno(file), &after) != 0) {
        result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_IO_ERROR, errno);
    } else if (!photoc_check_same_file(&before, &after)) {
        result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_FILE_CHANGED, 0);
    }
    if (fclose(file) != 0)
        result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_IO_ERROR, errno);
    return result;
}
