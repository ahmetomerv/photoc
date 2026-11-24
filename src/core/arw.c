#define _POSIX_C_SOURCE 200809L

#include "metadata_internal.h"
#include "tiff.h"
#include "photoc/arw.h"
#include "photoc/fs.h"

#include <errno.h>
#include <libexif/exif-content.h>
#include <libexif/exif-mem.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/* Only selected common entries are retained; MakerNotes/pixels are opaque. */
typedef struct {
    ExifMem *memory; /* Owned. Entries/data retain their own references. */
    ExifData *exif;  /* Owned. */
    uint64_t stored;
    uint32_t exif_width;
    uint32_t exif_height;
    uint32_t raw_width;
    uint32_t raw_height;
    uint32_t directory;
    uint32_t width;
    uint32_t height;
    uint32_t subfile;
    uint32_t photometric;
    bool other_raw;
} arw_metadata;

static void finish_image_directory(arw_metadata *metadata)
{
    if ((metadata->photometric == 32803 || metadata->photometric == 34892) &&
        (metadata->subfile & 1) == 0 && metadata->width != 0 &&
        metadata->height != 0 &&
        (uint64_t)metadata->width * metadata->height >
            (uint64_t)metadata->raw_width * metadata->raw_height) {
        metadata->raw_width = metadata->width;
        metadata->raw_height = metadata->height;
    }
}

static bool selected(const photoc_tiff_entry *entry, ExifIfd *ifd)
{
    if (entry->kind == PHOTOC_TIFF_IMAGE && entry->primary &&
        (entry->tag == EXIF_TAG_MAKE || entry->tag == EXIF_TAG_MODEL ||
         entry->tag == EXIF_TAG_ORIENTATION)) {
        *ifd = EXIF_IFD_0;
        return true;
    }
    if (entry->kind == PHOTOC_TIFF_EXIF &&
        (entry->tag == EXIF_TAG_DATE_TIME_ORIGINAL ||
         entry->tag == EXIF_TAG_ISO_SPEED_RATINGS ||
         entry->tag == EXIF_TAG_FNUMBER ||
         entry->tag == EXIF_TAG_EXPOSURE_TIME ||
         entry->tag == EXIF_TAG_FOCAL_LENGTH ||
         entry->tag == EXIF_TAG_LENS_MODEL ||
         entry->tag == EXIF_TAG_FOCAL_LENGTH_IN_35MM_FILM)) {
        *ifd = EXIF_IFD_EXIF;
        return true;
    }
    if (entry->kind == PHOTOC_TIFF_GPS &&
        (entry->tag == EXIF_TAG_GPS_LATITUDE_REF ||
         entry->tag == EXIF_TAG_GPS_LATITUDE ||
         entry->tag == EXIF_TAG_GPS_LONGITUDE_REF ||
         entry->tag == EXIF_TAG_GPS_LONGITUDE)) {
        *ifd = EXIF_IFD_GPS;
        return true;
    }
    return false;
}

static bool read_entry(photoc_tiff_reader *reader,
                       const photoc_tiff_entry *entry, void *data)
{
    arw_metadata *metadata = data;
    exif_data_set_byte_order(metadata->exif, reader->order);
    if (entry->directory_offset != metadata->directory) {
        finish_image_directory(metadata);
        metadata->directory = entry->directory_offset;
        metadata->width = 0;
        metadata->height = 0;
        metadata->subfile = 0;
        metadata->photometric = 0;
    }
    if (entry->kind == PHOTOC_TIFF_IMAGE) {
        if (entry->tag == 0x0100)
            return photoc_tiff_scalar(reader, entry, &metadata->width);
        if (entry->tag == 0x0101)
            return photoc_tiff_scalar(reader, entry, &metadata->height);
        if (entry->tag == 0x00fe)
            return photoc_tiff_scalar(reader, entry, &metadata->subfile);
        if (entry->tag == 0x0106)
            return photoc_tiff_scalar(reader, entry, &metadata->photometric);
        if (entry->tag == 0xc612) /* DNGVersion; DNG is not an ARW backend. */
            metadata->other_raw = true;
    } else if (entry->kind == PHOTOC_TIFF_EXIF) {
        if (entry->tag == EXIF_TAG_PIXEL_X_DIMENSION)
            return photoc_tiff_scalar(reader, entry, &metadata->exif_width);
        if (entry->tag == EXIF_TAG_PIXEL_Y_DIMENSION)
            return photoc_tiff_scalar(reader, entry, &metadata->exif_height);
    }
    ExifIfd ifd;
    if (!selected(entry, &ifd) || entry->size == 0)
        return true;
    if (entry->size > PHOTOC_TIFF_MAX_VALUE ||
        entry->size > UINT64_C(262144) - metadata->stored) {
        errno = EFBIG;
        return false;
    }
    if (exif_content_get_entry(metadata->exif->ifd[ifd], (ExifTag)entry->tag) !=
        NULL)
        return true; /* First standard directory wins. */
    ExifEntry *value = exif_entry_new_mem(metadata->memory);
    if (value == NULL) {
        errno = ENOMEM;
        return false;
    }
    value->tag = (ExifTag)entry->tag;
    value->format = (ExifFormat)entry->type;
    value->components = entry->count;
    value->size = (unsigned int)entry->size;
    value->data = exif_mem_alloc(metadata->memory, value->size);
    if (value->data == NULL) {
        exif_entry_unref(value);
        errno = ENOMEM;
        return false;
    }
    if (!photoc_tiff_read(reader, entry->offset, value->data, value->size)) {
        int saved_errno = errno;
        exif_entry_unref(value);
        errno = saved_errno;
        return false;
    }
    metadata->stored += entry->size;
    exif_content_add_entry(metadata->exif->ifd[ifd], value);
    bool added =
        exif_content_get_entry(metadata->exif->ifd[ifd], value->tag) == value;
    exif_entry_unref(value);
    if (!added)
        errno = ENOMEM;
    return added;
}

static bool sony_make(const char *make)
{
    if (make == NULL)
        return true; /* Absent metadata does not invent a manufacturer. */
    return photoc_fs_compare_casefold(make, "SONY") == 0;
}

photoc_metadata_result photoc_arw_load_metadata(FILE *file, uint64_t size,
                                                Photo *photo)
{
    arw_metadata metadata = {0};
    metadata.memory = exif_mem_new_default();
    if (metadata.memory == NULL)
        return PHOTOC_METADATA_NO_MEMORY;
    metadata.exif = exif_data_new_mem(metadata.memory);
    if (metadata.exif == NULL) {
        exif_mem_unref(metadata.memory);
        return PHOTOC_METADATA_NO_MEMORY;
    }
    errno = 0;
    int status = photoc_tiff_walk(file, size, read_entry, &metadata);
    int saved_errno = errno;
    photoc_metadata_result result = PHOTOC_METADATA_OK;
    if (status != 0) {
        result = saved_errno == ENOMEM    ? PHOTOC_METADATA_NO_MEMORY
                 : saved_errno == EFBIG   ? PHOTOC_METADATA_RESOURCE_LIMIT
                 : saved_errno == ENOTSUP ? PHOTOC_METADATA_UNSUPPORTED_FORMAT
                 : saved_errno == EINVAL  ? PHOTOC_METADATA_INVALID_ARW
                                          : PHOTOC_METADATA_IO_ERROR;
    } else if (!photoc_metadata_read_exif(metadata.exif, photo)) {
        result = PHOTOC_METADATA_NO_MEMORY;
    } else if (metadata.other_raw || !sony_make(photo->camera_make)) {
        result = PHOTOC_METADATA_UNSUPPORTED_FORMAT;
    } else {
        finish_image_directory(&metadata);
        uint32_t width = metadata.exif_width;
        uint32_t height = metadata.exif_height;
        if ((width == 0 || height == 0) && metadata.raw_width != 0) {
            width = metadata.raw_width;
            height = metadata.raw_height;
        }
        photo->width = width;
        photo->height = height;
        photo->has_width = width != 0;
        photo->has_height = height != 0;
    }
    exif_data_unref(metadata.exif);
    exif_mem_unref(metadata.memory);
    errno = saved_errno;
    return result;
}

/* JPEGInterchangeFormat/Length pairs per image directory; the largest valid
   pair outside CFA/linear RAW directories is the preview. */
typedef struct {
    uint32_t directory;
    uint32_t offset;
    uint32_t length;
    uint32_t photometric;
    uint32_t best_offset;
    uint32_t best_length;
} preview_search;

static void finish_preview_directory(preview_search *search)
{
    if (search->offset != 0 && search->length > search->best_length &&
        search->photometric != 32803 && search->photometric != 34892) {
        search->best_offset = search->offset;
        search->best_length = search->length;
    }
}

static bool find_preview(photoc_tiff_reader *reader,
                         const photoc_tiff_entry *entry, void *data)
{
    preview_search *search = data;
    if (entry->kind != PHOTOC_TIFF_IMAGE)
        return true;
    if (entry->directory_offset != search->directory) {
        finish_preview_directory(search);
        search->directory = entry->directory_offset;
        search->offset = 0;
        search->length = 0;
        search->photometric = 0;
    }
    if (entry->tag == 0x0201)
        return photoc_tiff_scalar(reader, entry, &search->offset);
    if (entry->tag == 0x0202)
        return photoc_tiff_scalar(reader, entry, &search->length);
    if (entry->tag == 0x0106)
        return photoc_tiff_scalar(reader, entry, &search->photometric);
    return true;
}

photoc_image_result photoc_arw_read_preview(const char *path,
                                            uint64_t max_bytes,
                                            unsigned char **jpeg,
                                            size_t *length)
{
    if (jpeg == NULL || length == NULL)
        return PHOTOC_IMAGE_INVALID_ARGUMENT;
    *jpeg = NULL;
    *length = 0;
    if (path == NULL || path[0] == '\0')
        return PHOTOC_IMAGE_INVALID_ARGUMENT;
    if (max_bytes == 0)
        max_bytes = PHOTOC_ARW_PREVIEW_MAX_BYTES;
    FILE *file = NULL;
    uint64_t size = 0;
    if (photoc_fs_open_regular(path, &file, &size) != 0)
        return PHOTOC_IMAGE_IO_ERROR;

    preview_search search = {0};
    errno = 0;
    photoc_image_result result = PHOTOC_IMAGE_OK;
    if (photoc_tiff_walk(file, size, find_preview, &search) != 0) {
        result = errno == ENOMEM  ? PHOTOC_IMAGE_NO_MEMORY
                 : errno == EFBIG ? PHOTOC_IMAGE_TOO_LARGE
                 : errno == EINVAL || errno == ENOTSUP
                     ? PHOTOC_IMAGE_INVALID_ARW
                     : PHOTOC_IMAGE_IO_ERROR;
    } else {
        finish_preview_directory(&search);
        uint64_t offset = search.best_offset;
        uint64_t bytes = search.best_length;
        if (bytes == 0 || offset < 8 || offset > size || bytes > size - offset)
            result = PHOTOC_IMAGE_NO_PREVIEW;
        else if (bytes > max_bytes || bytes > SIZE_MAX)
            result = PHOTOC_IMAGE_TOO_LARGE;
    }
    unsigned char *data = NULL;
    if (result == PHOTOC_IMAGE_OK) {
        data = malloc((size_t)search.best_length);
        if (data == NULL)
            result = PHOTOC_IMAGE_NO_MEMORY;
    }
    if (result == PHOTOC_IMAGE_OK) {
        errno = 0;
        if (fseeko(file, (off_t)search.best_offset, SEEK_SET) != 0 ||
            fread(data, 1, search.best_length, file) != search.best_length) {
            if (errno == 0)
                errno = EIO;
            result = PHOTOC_IMAGE_IO_ERROR;
        } else if (search.best_length < 2 || data[0] != 0xff ||
                   data[1] != 0xd8) {
            result = PHOTOC_IMAGE_NO_PREVIEW;
        }
    }
    int saved_errno = errno;
    if (fclose(file) != 0 && result == PHOTOC_IMAGE_OK) {
        saved_errno = errno;
        result = PHOTOC_IMAGE_IO_ERROR;
    }
    if (result != PHOTOC_IMAGE_OK) {
        free(data);
        errno = saved_errno;
        return result;
    }
    *jpeg = data;
    *length = search.best_length;
    return PHOTOC_IMAGE_OK;
}
