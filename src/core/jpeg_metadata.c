#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/jpeg_metadata.h"

#include "jpeg.h"
#include "jpeg_metadata_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define METADATA_MEMORY_LIMIT (64u * 1024u * 1024u)

/* Flexible payload storage is owned by the enclosing snapshot. */
typedef struct metadata_segment {
    struct metadata_segment *next;
    unsigned int length;
    unsigned char marker;
    unsigned char payload[];
} metadata_segment;

struct photoc_jpeg_metadata {
    metadata_segment *first;
    metadata_segment *last;
    size_t allocated;
    uint64_t overhead;
    uint32_t width;
    uint32_t height;
    unsigned int components;
    bool has_icc;
};

typedef struct {
    photoc_jpeg_metadata *metadata;
    photoc_jpeg_edit_result result;
    bool exif_seen;
    bool icc_seen[256];
    unsigned int icc_count;
    uint64_t icc_bytes;
} metadata_reader;

void photoc_jpeg_metadata_free(photoc_jpeg_metadata *metadata)
{
    if (metadata == NULL) {
        return;
    }
    metadata_segment *segment = metadata->first;
    while (segment != NULL) {
        metadata_segment *next = segment->next;
        free(segment);
        segment = next;
    }
    free(metadata);
}

uint64_t
photoc_jpeg_metadata_output_overhead(const photoc_jpeg_metadata *metadata)
{
    return metadata == NULL ? 0 : metadata->overhead;
}

bool photoc_jpeg_metadata_has_grayscale_icc(
    const photoc_jpeg_metadata *metadata)
{
    return metadata != NULL && metadata->has_icc && metadata->components == 1;
}

bool photoc_jpeg_metadata_has_icc(const photoc_jpeg_metadata *metadata)
{
    return metadata != NULL && metadata->has_icc;
}

static bool signature_matches(const unsigned char *bytes, unsigned int length,
                              const char *signature, size_t signature_length)
{
    return length >= signature_length &&
           memcmp(bytes, signature, signature_length) == 0;
}

static int collect_segment(unsigned char marker, const unsigned char *bytes,
                           unsigned int length, bool after_scan,
                           void *user_data)
{
    metadata_reader *reader = user_data;
    bool exif =
        marker == 0xe1 && signature_matches(bytes, length, "Exif\0\0", 6);
    bool xmp =
        marker == 0xe1 &&
        (signature_matches(bytes, length, "http://ns.adobe.com/xap/1.0/",
                           sizeof("http://ns.adobe.com/xap/1.0/")) ||
         signature_matches(bytes, length, "http://ns.adobe.com/xmp/extension/",
                           sizeof("http://ns.adobe.com/xmp/extension/")));
    bool icc = marker == 0xe2 && signature_matches(bytes, length, "ICC_PROFILE",
                                                   sizeof("ICC_PROFILE") - 1);
    if (!exif && !xmp && !icc) {
        return 0;
    }
    if (after_scan) {
        reader->result = exif ? PHOTOC_JPEG_EDIT_UNSAFE_LAYOUT
                              : PHOTOC_JPEG_EDIT_UNSAFE_METADATA_LAYOUT;
        return -1;
    }
    if (exif) {
        if (reader->exif_seen) {
            reader->result = PHOTOC_JPEG_EDIT_UNSAFE_LAYOUT;
            return -1;
        }
        reader->exif_seen = true;
        reader->result = photoc_jpeg_exif_validate_segment(bytes, length);
        if (reader->result != PHOTOC_JPEG_EDIT_OK) {
            return -1;
        }
    }
    if (icc) {
        if (length < 14 || bytes[11] != 0 || bytes[12] == 0 || bytes[13] == 0 ||
            bytes[12] > bytes[13] || reader->icc_seen[bytes[12]] ||
            (reader->icc_count != 0 && reader->icc_count != bytes[13])) {
            reader->result = PHOTOC_JPEG_EDIT_INVALID_ICC;
            return -1;
        }
        reader->icc_count = bytes[13];
        reader->icc_seen[bytes[12]] = true;
        reader->icc_bytes += length - 14;
    }
    photoc_jpeg_metadata *metadata = reader->metadata;
    size_t allocation = sizeof(metadata_segment) + (size_t)length;
    if (allocation > METADATA_MEMORY_LIMIT - metadata->allocated) {
        reader->result = PHOTOC_JPEG_EDIT_METADATA_TOO_LARGE;
        return -1;
    }
    metadata_segment *segment = malloc(allocation);
    if (segment == NULL) {
        reader->result = PHOTOC_JPEG_EDIT_NO_MEMORY;
        return -1;
    }
    segment->next = NULL;
    segment->marker = marker;
    segment->length = length;
    memcpy(segment->payload, bytes, length);
    if (metadata->last == NULL) {
        metadata->first = segment;
    } else {
        metadata->last->next = segment;
    }
    metadata->last = segment;
    metadata->allocated += allocation;
    metadata->overhead += (uint64_t)length + 4;
    return 0;
}

/* Scan an open JPEG stream into a fresh snapshot. *out is set only on
   success; the caller owns that memory and must close the stream itself
   (freeing *out if the close fails). */
static photoc_jpeg_edit_result metadata_scan(FILE *file,
                                             photoc_jpeg_metadata **out)
{
    *out = NULL;
    photoc_jpeg_metadata *metadata = calloc(1, sizeof(*metadata));
    if (metadata == NULL) {
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    }
    metadata_reader reader = {.metadata = metadata,
                              .result = PHOTOC_JPEG_EDIT_OK};
    photoc_jpeg_info info;
    if (photoc_jpeg_inspect_app(file, &info, collect_segment, &reader) != 0 &&
        reader.result == PHOTOC_JPEG_EDIT_OK) {
        reader.result = errno == EINVAL ? PHOTOC_JPEG_EDIT_INVALID_JPEG
                                        : PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    if (reader.result == PHOTOC_JPEG_EDIT_OK && reader.icc_count != 0) {
        for (unsigned int i = 1; i <= reader.icc_count; ++i) {
            if (!reader.icc_seen[i]) {
                reader.result = PHOTOC_JPEG_EDIT_INVALID_ICC;
                break;
            }
        }
        if (reader.icc_bytes == 0) {
            reader.result = PHOTOC_JPEG_EDIT_INVALID_ICC;
        }
    }
    if (reader.result != PHOTOC_JPEG_EDIT_OK) {
        photoc_jpeg_metadata_free(metadata);
        return reader.result;
    }
    metadata->width = info.width;
    metadata->height = info.height;
    metadata->components = info.components;
    metadata->has_icc = reader.icc_count != 0;
    *out = metadata;
    return PHOTOC_JPEG_EDIT_OK;
}

photoc_jpeg_edit_result
photoc_jpeg_metadata_load_copy(const char *path, photoc_jpeg_metadata **out)
{
    if (out == NULL) {
        return PHOTOC_JPEG_EDIT_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (path == NULL || path[0] == '\0') {
        return PHOTOC_JPEG_EDIT_INVALID_ARGUMENT;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    photoc_jpeg_edit_result result = metadata_scan(file, out);
    int saved_errno = errno;
    if (fclose(file) != 0 && result == PHOTOC_JPEG_EDIT_OK) {
        photoc_jpeg_metadata_free(*out);
        *out = NULL;
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
        saved_errno = errno;
    }
    errno = saved_errno;
    return result;
}

photoc_jpeg_edit_result
photoc_jpeg_metadata_load_copy_buffer(const unsigned char *bytes, size_t length,
                                      photoc_jpeg_metadata **out)
{
    if (out == NULL) {
        return PHOTOC_JPEG_EDIT_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (bytes == NULL) {
        return PHOTOC_JPEG_EDIT_INVALID_ARGUMENT;
    }
    if (length == 0) {
        return PHOTOC_JPEG_EDIT_INVALID_JPEG;
    }
    /* fmemopen reads from the borrowed buffer; ftello/fseeko let the existing
       stream parser walk it without copying the JPEG again. */
    FILE *file = fmemopen((void *)bytes, length, "rb");
    if (file == NULL) {
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    photoc_jpeg_edit_result result = metadata_scan(file, out);
    int saved_errno = errno;
    if (fclose(file) != 0 && result == PHOTOC_JPEG_EDIT_OK) {
        photoc_jpeg_metadata_free(*out);
        *out = NULL;
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
        saved_errno = errno;
    }
    errno = saved_errno;
    return result;
}

static int write_bytes(FILE *file, const void *bytes, size_t length)
{
    if (length == 0) {
        return 0;
    }
    errno = 0;
    if (fwrite(bytes, length, 1, file) != 1) {
        if (errno == 0) {
            errno = EIO;
        }
        return -1;
    }
    return 0;
}

photoc_jpeg_edit_result
photoc_jpeg_metadata_write_encoded(FILE *file,
                                   const photoc_jpeg_buffer *encoded,
                                   const photoc_jpeg_metadata *metadata)
{
    /* Preserve the encoder's leading APP0/JFIF markers, then insert metadata.
       Other original APP markers (e.g. Adobe encoding transforms) are excluded. */
    if (encoded->size < 2 || encoded->data[0] != 0xff ||
        encoded->data[1] != 0xd8) {
        return PHOTOC_JPEG_EDIT_INVALID_JPEG;
    }
    size_t prefix = 2;
    while (encoded->size - prefix >= 4 && encoded->data[prefix] == 0xff &&
           encoded->data[prefix + 1] == 0xe0) {
        size_t length = ((size_t)encoded->data[prefix + 2] << 8) |
                        encoded->data[prefix + 3];
        if (length < 2 || length > encoded->size - prefix - 2) {
            return PHOTOC_JPEG_EDIT_INVALID_JPEG;
        }
        prefix += length + 2;
    }
    if (write_bytes(file, encoded->data, prefix) != 0) {
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    for (const metadata_segment *segment = metadata->first; segment != NULL;
         segment = segment->next) {
        unsigned int length = segment->length + 2;
        const unsigned char header[] = {0xff, segment->marker,
                                        (unsigned char)(length >> 8),
                                        (unsigned char)length};
        if (write_bytes(file, header, sizeof(header)) != 0 ||
            write_bytes(file, segment->payload, segment->length) != 0) {
            return PHOTOC_JPEG_EDIT_IO_ERROR;
        }
    }
    return write_bytes(file, encoded->data + prefix, encoded->size - prefix) ==
                   0
               ? PHOTOC_JPEG_EDIT_OK
               : PHOTOC_JPEG_EDIT_IO_ERROR;
}

photoc_jpeg_edit_result
photoc_jpeg_metadata_verify(const photoc_jpeg_metadata *metadata,
                            const char *path)
{
    photoc_jpeg_metadata *written = NULL;
    photoc_jpeg_edit_result result =
        photoc_jpeg_metadata_load_copy(path, &written);
    if (result != PHOTOC_JPEG_EDIT_OK) {
        return result;
    }
    if (written->width != metadata->width ||
        written->height != metadata->height ||
        (metadata->has_icc && written->components != metadata->components)) {
        result = PHOTOC_JPEG_EDIT_INVALID_JPEG;
    }
    const metadata_segment *left = metadata->first;
    const metadata_segment *right = written->first;
    while (result == PHOTOC_JPEG_EDIT_OK && left != NULL && right != NULL) {
        if (left->marker != right->marker || left->length != right->length ||
            memcmp(left->payload, right->payload, left->length) != 0) {
            result = PHOTOC_JPEG_EDIT_UNSAFE_METADATA_LAYOUT;
            break;
        }
        left = left->next;
        right = right->next;
    }
    if (result == PHOTOC_JPEG_EDIT_OK && (left != NULL || right != NULL)) {
        result = PHOTOC_JPEG_EDIT_UNSAFE_METADATA_LAYOUT;
    }
    photoc_jpeg_metadata_free(written);
    if (result == PHOTOC_JPEG_EDIT_OK) {
        photoc_image image = {0};
        photoc_image_result decoded = photoc_image_decode_jpeg(path, &image);
        if (decoded != PHOTOC_IMAGE_OK) {
            result = decoded == PHOTOC_IMAGE_NO_MEMORY
                         ? PHOTOC_JPEG_EDIT_NO_MEMORY
                     : decoded == PHOTOC_IMAGE_IO_ERROR
                         ? PHOTOC_JPEG_EDIT_IO_ERROR
                         : PHOTOC_JPEG_EDIT_INVALID_JPEG;
        } else if (image.width != metadata->width ||
                   image.height != metadata->height) {
            result = PHOTOC_JPEG_EDIT_INVALID_JPEG;
        }
        int saved_errno = errno;
        photoc_image_cleanup(&image);
        errno = saved_errno;
    }
    return result;
}
