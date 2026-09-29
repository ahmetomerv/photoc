#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/jpeg_write.h"

#include "photoc/fs.h"
#include "jpeg.h"
#include "jpeg_metadata_internal.h"

#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <libexif/exif-content.h>
#include <libexif/exif-data.h>
#include <libexif/exif-log.h>
#include <libexif/exif-mem.h>
#include <libexif/exif-utils.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct photoc_jpeg_exif {
    ExifMem *memory;
    ExifData *data;
    struct stat source_info;
    bool has_source_info;
};

static bool same_source(const struct stat *left, const struct stat *right)
{
#if defined(__APPLE__)
    const struct timespec *left_mtime = &left->st_mtimespec;
    const struct timespec *right_mtime = &right->st_mtimespec;
    const struct timespec *left_ctime = &left->st_ctimespec;
    const struct timespec *right_ctime = &right->st_ctimespec;
#else
    const struct timespec *left_mtime = &left->st_mtim;
    const struct timespec *right_mtime = &right->st_mtim;
    const struct timespec *left_ctime = &left->st_ctim;
    const struct timespec *right_ctime = &right->st_ctim;
#endif
    return left->st_dev == right->st_dev && left->st_ino == right->st_ino &&
           left->st_size == right->st_size && left->st_mode == right->st_mode &&
           left->st_uid == right->st_uid && left->st_gid == right->st_gid &&
           left->st_nlink == right->st_nlink &&
           left_mtime->tv_sec == right_mtime->tv_sec &&
           left_mtime->tv_nsec == right_mtime->tv_nsec &&
           left_ctime->tv_sec == right_ctime->tv_sec &&
           left_ctime->tv_nsec == right_ctime->tv_nsec;
}

void photoc_jpeg_exif_free(photoc_jpeg_exif *exif)
{
    if (exif == NULL) {
        return;
    }
    if (exif->data != NULL) {
        exif_data_unref(exif->data);
    }
    if (exif->memory != NULL) {
        exif_mem_unref(exif->memory);
    }
    free(exif);
}

typedef struct {
    bool corrupt;
    bool no_memory;
} exif_audit;

static void audit_log(ExifLog *log, ExifLogCode code, const char *domain,
                      const char *format, va_list arguments, void *user_data)
{
    (void)log;
    (void)domain;
    (void)format;
    (void)arguments;
    exif_audit *audit = user_data;
    audit->corrupt |= code == EXIF_LOG_CODE_CORRUPT_DATA;
    audit->no_memory |= code == EXIF_LOG_CODE_NO_MEMORY;
}

/* Audit standard TIFF directory/value bounds before libexif can silently skip
   broken entries. MakerNotes and tag meanings remain opaque. A fixed queue
   bounds cycles and pathological directory graphs. */
static bool audit_tiff(const unsigned char *tiff, size_t length)
{
    ExifByteOrder order =
        tiff[0] == 'I' ? EXIF_BYTE_ORDER_INTEL : EXIF_BYTE_ORDER_MOTOROLA;
    uint32_t offsets[64] = {exif_get_long(tiff + 4, order)};
    size_t count = 1;
    uint64_t stored = 0;
    for (size_t i = 0; i < count; ++i) {
        uint32_t offset = offsets[i];
        if (offset < 8 || offset > length || length - offset < 2)
            return false;
        for (size_t j = 0; j < i; ++j) {
            if (offsets[j] == offset)
                return false;
        }
        unsigned int entries = exif_get_short(tiff + offset, order);
        size_t available = length - offset - 2;
        if (available < 4 || entries > (available - 4) / 12)
            return false;
        for (unsigned int j = 0; j < entries; ++j) {
            const unsigned char *entry = tiff + offset + 2 + (size_t)j * 12;
            unsigned int tag = exif_get_short(entry, order);
            unsigned int format = exif_get_short(entry + 2, order);
            uint32_t components = exif_get_long(entry + 4, order);
            unsigned int unit = exif_format_get_size((ExifFormat)format);
            if (unit == 0)
                return false;
            uint64_t bytes = (uint64_t)components * unit;
            /* Repeated references to a small payload must not make libexif
               allocate an unbounded number of copies of the same values. */
            const uint64_t budget = UINT64_C(4194304);
            if (bytes > budget - stored ||
                sizeof(ExifEntry) + sizeof(ExifEntry *) >
                    budget - stored - bytes) {
                return false;
            }
            stored += bytes + sizeof(ExifEntry) + sizeof(ExifEntry *);
            uint32_t value = exif_get_long(entry + 8, order);
            if (bytes > 4 && (value > length || bytes > length - value))
                return false;
            if (tag == 0x8769 || tag == 0x8825 || tag == 0xa005) {
                if (format != EXIF_FORMAT_LONG || components != 1)
                    return false;
                if (value != 0) {
                    if (count == sizeof(offsets) / sizeof(offsets[0]))
                        return false;
                    offsets[count++] = value;
                }
            }
        }
        uint32_t next =
            exif_get_long(tiff + offset + 2 + (size_t)entries * 12, order);
        if (next != 0) {
            if (count == sizeof(offsets) / sizeof(offsets[0]))
                return false;
            offsets[count++] = next;
        }
    }
    return true;
}

static photoc_jpeg_edit_result parse_exif_segment(const unsigned char *bytes,
                                                  unsigned int length,
                                                  photoc_jpeg_exif **out,
                                                  bool auditing)
{
    if (length < 14 || memcmp(bytes, "Exif\0\0", 6) != 0 ||
        !((bytes[6] == 'I' && bytes[7] == 'I' && bytes[8] == 0x2a &&
           bytes[9] == 0) ||
          (bytes[6] == 'M' && bytes[7] == 'M' && bytes[8] == 0 &&
           bytes[9] == 0x2a))) {
        return PHOTOC_JPEG_EDIT_INVALID_EXIF;
    }

    if (auditing && !audit_tiff(bytes + 6, length - 6)) {
        return PHOTOC_JPEG_EDIT_INVALID_EXIF;
    }
    photoc_jpeg_exif *exif = calloc(1, sizeof(*exif));
    if (exif == NULL) {
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    }
    exif->memory = exif_mem_new_default();
    if (exif->memory != NULL) {
        exif->data = exif_data_new_mem(exif->memory);
    }
    if (exif->data == NULL) {
        photoc_jpeg_exif_free(exif);
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    }
    /* Keep nonstandard tags where libexif can represent them. Do not run
       automatic EXIF repairs that could discard unrelated metadata. */
    exif_data_unset_option(exif->data, EXIF_DATA_OPTION_IGNORE_UNKNOWN_TAGS);
    exif_data_unset_option(exif->data, EXIF_DATA_OPTION_FOLLOW_SPECIFICATION);
    exif_audit audit = {0};
    ExifLog *log = auditing ? exif_log_new_mem(exif->memory) : NULL;
    if (auditing && log == NULL) {
        photoc_jpeg_exif_free(exif);
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    }
    if (log != NULL) {
        exif_log_set_func(log, audit_log, &audit);
        exif_data_log(exif->data, log);
    }
    exif_data_load_data(exif->data, bytes, length);
    if (log != NULL) {
        exif_data_log(exif->data, NULL);
        exif_log_unref(log);
    }
    if (audit.no_memory || audit.corrupt) {
        photoc_jpeg_exif_free(exif);
        return audit.no_memory ? PHOTOC_JPEG_EDIT_NO_MEMORY
                               : PHOTOC_JPEG_EDIT_INVALID_EXIF;
    }
    bool has_entries = false;
    for (size_t i = 0; i < EXIF_IFD_COUNT; ++i) {
        if (exif->data->ifd[i] != NULL && exif->data->ifd[i]->count != 0) {
            has_entries = true;
            break;
        }
    }
    if (!has_entries) {
        photoc_jpeg_exif_free(exif);
        return PHOTOC_JPEG_EDIT_INVALID_EXIF;
    }
    *out = exif;
    return PHOTOC_JPEG_EDIT_OK;
}

static photoc_jpeg_edit_result parse_exif(const unsigned char *bytes,
                                          unsigned int length,
                                          photoc_jpeg_exif **out)
{
    return parse_exif_segment(bytes, length, out, false);
}

photoc_jpeg_edit_result
photoc_jpeg_exif_audit_segment(const unsigned char *bytes, unsigned int length)
{
    photoc_jpeg_exif *exif = NULL;
    photoc_jpeg_edit_result result =
        parse_exif_segment(bytes, length, &exif, true);
    photoc_jpeg_exif_free(exif);
    return result;
}

photoc_jpeg_edit_result
photoc_jpeg_exif_validate_segment(const unsigned char *bytes,
                                  unsigned int length)
{
    photoc_jpeg_exif *exif = NULL;
    photoc_jpeg_edit_result result = parse_exif(bytes, length, &exif);
    photoc_jpeg_exif_free(exif);
    return result;
}

static photoc_jpeg_edit_result inspect_file(FILE *file, photoc_jpeg_info *info)
{
    if (photoc_jpeg_inspect(file, info) == 0) {
        return PHOTOC_JPEG_EDIT_OK;
    }
    return errno == EINVAL ? PHOTOC_JPEG_EDIT_INVALID_JPEG
                           : PHOTOC_JPEG_EDIT_IO_ERROR;
}

static photoc_jpeg_edit_result
read_exif(FILE *file, const photoc_jpeg_info *info, photoc_jpeg_exif **out)
{
    if (info->exif_count == 0) {
        return PHOTOC_JPEG_EDIT_NO_EXIF;
    }
    if (info->exif_count != 1 || info->exif_after_scan) {
        return PHOTOC_JPEG_EDIT_UNSAFE_LAYOUT;
    }
    if (info->exif_offset > INT64_MAX - 4) {
        errno = EOVERFLOW;
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    if (fseeko(file, (off_t)(info->exif_offset + 4), SEEK_SET) != 0) {
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    if (info->exif_length < 4) {
        return PHOTOC_JPEG_EDIT_INVALID_JPEG;
    }
    unsigned int length = (unsigned int)(info->exif_length - 4);
    unsigned char *bytes = malloc(length);
    if (bytes == NULL) {
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    }
    photoc_jpeg_edit_result result;
    errno = 0;
    if (fread(bytes, 1, length, file) != length) {
        if (errno == 0) {
            errno = EIO;
        }
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
    } else {
        result = parse_exif(bytes, length, out);
    }
    free(bytes);
    return result;
}

photoc_jpeg_edit_result photoc_jpeg_exif_load_copy(const char *source_path,
                                                   photoc_jpeg_exif **out)
{
    if (out == NULL || source_path == NULL || source_path[0] == '\0') {
        return PHOTOC_JPEG_EDIT_INVALID_ARGUMENT;
    }
    *out = NULL;
    FILE *file = fopen(source_path, "rb");
    if (file == NULL) {
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    struct stat before;
    if (fstat(fileno(file), &before) != 0) {
        int saved_errno = errno;
        fclose(file);
        errno = saved_errno;
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    photoc_jpeg_info info;
    photoc_jpeg_edit_result result = inspect_file(file, &info);
    if (result == PHOTOC_JPEG_EDIT_OK) {
        result = read_exif(file, &info, out);
    }
    if (result == PHOTOC_JPEG_EDIT_OK) {
        struct stat after;
        if (fstat(fileno(file), &after) != 0) {
            photoc_jpeg_exif_free(*out);
            *out = NULL;
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        } else if (!same_source(&before, &after)) {
            photoc_jpeg_exif_free(*out);
            *out = NULL;
            result = PHOTOC_JPEG_EDIT_UNSAFE_SOURCE;
        } else {
            (*out)->source_info = before;
            (*out)->has_source_info = true;
        }
    }
    int saved_errno = errno;
    if (fclose(file) != 0 && result == PHOTOC_JPEG_EDIT_OK) {
        photoc_jpeg_exif_free(*out);
        *out = NULL;
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    errno = saved_errno;
    return result;
}

static photoc_jpeg_edit_result serialize_exif(const photoc_jpeg_exif *exif,
                                              unsigned char **bytes,
                                              unsigned int *length)
{
    *bytes = NULL;
    *length = 0;
    exif_data_save_data(exif->data, bytes, length);
    if (*bytes == NULL || *length == 0) {
        if (*bytes != NULL) {
            exif_mem_free(exif->memory, *bytes);
            *bytes = NULL;
        }
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    }
    if (*length > 65533u) {
        exif_mem_free(exif->memory, *bytes);
        *bytes = NULL;
        return PHOTOC_JPEG_EDIT_EXIF_TOO_LARGE;
    }
    if (*length < 6 || memcmp(*bytes, "Exif\0\0", 6) != 0) {
        exif_mem_free(exif->memory, *bytes);
        *bytes = NULL;
        return PHOTOC_JPEG_EDIT_INVALID_EXIF;
    }
    return PHOTOC_JPEG_EDIT_OK;
}

photoc_jpeg_edit_result
photoc_jpeg_exif_output_overhead(const photoc_jpeg_exif *exif, uint64_t *bytes)
{
    if (bytes == NULL) {
        return PHOTOC_JPEG_EDIT_INVALID_ARGUMENT;
    }
    if (exif == NULL) {
        *bytes = 0;
        return PHOTOC_JPEG_EDIT_OK;
    }
    unsigned char *serialized = NULL;
    unsigned int length = 0;
    photoc_jpeg_edit_result result = serialize_exif(exif, &serialized, &length);
    if (result == PHOTOC_JPEG_EDIT_OK) {
        *bytes = (uint64_t)length + 4;
        exif_mem_free(exif->memory, serialized);
    }
    return result;
}

photoc_jpeg_edit_result photoc_jpeg_exif_copy(const photoc_jpeg_exif *source,
                                              photoc_jpeg_exif **out)
{
    if (source == NULL || out == NULL || source->data == NULL ||
        source->memory == NULL) {
        return PHOTOC_JPEG_EDIT_INVALID_ARGUMENT;
    }
    *out = NULL;
    unsigned char *bytes;
    unsigned int length;
    photoc_jpeg_edit_result result = serialize_exif(source, &bytes, &length);
    if (result == PHOTOC_JPEG_EDIT_OK) {
        result = parse_exif(bytes, length, out);
        if (result == PHOTOC_JPEG_EDIT_OK) {
            (*out)->source_info = source->source_info;
            (*out)->has_source_info = source->has_source_info;
        }
        exif_mem_free(source->memory, bytes);
    }
    return result;
}

photoc_jpeg_edit_result photoc_jpeg_exif_remove_gps(photoc_jpeg_exif *exif)
{
    if (exif == NULL || exif->data == NULL) {
        return PHOTOC_JPEG_EDIT_INVALID_ARGUMENT;
    }
    ExifContent *gps = exif->data->ifd[EXIF_IFD_GPS];
    if (gps == NULL) {
        return PHOTOC_JPEG_EDIT_INVALID_EXIF;
    }
    while (gps->count != 0) {
        exif_content_remove_entry(gps, gps->entries[0]);
    }
    ExifContent *root = exif->data->ifd[EXIF_IFD_0];
    if (root == NULL) {
        return PHOTOC_JPEG_EDIT_INVALID_EXIF;
    }
    ExifEntry *pointer =
        exif_content_get_entry(root, EXIF_TAG_GPS_INFO_IFD_POINTER);
    if (pointer != NULL) {
        exif_content_remove_entry(root, pointer);
    }
    return PHOTOC_JPEG_EDIT_OK;
}

bool photoc_jpeg_exif_has_gps(const photoc_jpeg_exif *exif)
{
    if (exif == NULL || exif->data == NULL) {
        return false;
    }
    ExifContent *gps = exif->data->ifd[EXIF_IFD_GPS];
    ExifContent *root = exif->data->ifd[EXIF_IFD_0];
    return (gps != NULL && gps->count != 0) ||
           (root != NULL && exif_content_get_entry(
                                root, EXIF_TAG_GPS_INFO_IFD_POINTER) != NULL);
}

static photoc_jpeg_edit_result destination_directory(const char *destination,
                                                     char **directory)
{
    const char *slash = strrchr(destination, '/');
    size_t length = slash == NULL          ? 1
                    : slash == destination ? 1
                                           : (size_t)(slash - destination);
    char *copy = malloc(length + 1);
    if (copy == NULL) {
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    }
    if (slash == NULL) {
        copy[0] = '.';
    } else {
        memcpy(copy, destination, length);
    }
    copy[length] = '\0';
    *directory = copy;
    return PHOTOC_JPEG_EDIT_OK;
}

static int copy_bytes(FILE *source, FILE *destination, uint64_t count)
{
    unsigned char buffer[16384];
    while (count != 0) {
        size_t chunk = count < sizeof(buffer) ? (size_t)count : sizeof(buffer);
        errno = 0;
        if (fread(buffer, 1, chunk, source) != chunk ||
            fwrite(buffer, 1, chunk, destination) != chunk) {
            if (errno == 0) {
                errno = EIO;
            }
            return -1;
        }
        count -= chunk;
    }
    return 0;
}

static int copy_to_eof(FILE *source, FILE *destination)
{
    unsigned char buffer[16384];
    size_t count;
    errno = 0;
    while ((count = fread(buffer, 1, sizeof(buffer), source)) != 0) {
        if (fwrite(buffer, 1, count, destination) != count) {
            if (errno == 0) {
                errno = EIO;
            }
            return -1;
        }
        errno = 0;
    }
    if (ferror(source)) {
        if (errno == 0) {
            errno = EIO;
        }
        return -1;
    }
    return 0;
}

static int write_jpeg(FILE *source, FILE *destination,
                      const photoc_jpeg_info *info,
                      const unsigned char *exif_bytes, unsigned int length)
{
    uint64_t prefix =
        info->exif_count == 0 ? info->exif_insert_offset : info->exif_offset;
    if (fseeko(source, 0, SEEK_SET) != 0 ||
        copy_bytes(source, destination, prefix) != 0) {
        return -1;
    }
    unsigned int segment_length = length + 2;
    unsigned char header[4] = {0xff, 0xe1, (unsigned char)(segment_length >> 8),
                               (unsigned char)segment_length};
    errno = 0;
    if (fwrite(header, 1, sizeof(header), destination) != sizeof(header) ||
        fwrite(exif_bytes, 1, length, destination) != length) {
        if (errno == 0) {
            errno = EIO;
        }
        return -1;
    }
    uint64_t suffix = info->exif_count == 0
                          ? info->exif_insert_offset
                          : info->exif_offset + info->exif_length;
    if (suffix > INT64_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    if (fseeko(source, (off_t)suffix, SEEK_SET) != 0) {
        return -1;
    }
    return copy_to_eof(source, destination);
}

static int compare_regions(FILE *source, FILE *written, uint64_t source_offset,
                           uint64_t written_offset, uint64_t length,
                           bool until_eof)
{
    if (source_offset > INT64_MAX || written_offset > INT64_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    if (fseeko(source, (off_t)source_offset, SEEK_SET) != 0 ||
        fseeko(written, (off_t)written_offset, SEEK_SET) != 0) {
        return -1;
    }
    unsigned char left[16384];
    unsigned char right[16384];
    do {
        size_t chunk =
            until_eof || length > sizeof(left) ? sizeof(left) : (size_t)length;
        size_t left_count = fread(left, 1, chunk, source);
        size_t right_count = fread(right, 1, chunk, written);
        if (ferror(source) || ferror(written)) {
            if (errno == 0) {
                errno = EIO;
            }
            return -1;
        }
        if (left_count != right_count || memcmp(left, right, left_count) != 0 ||
            (!until_eof && left_count != chunk)) {
            errno = EIO;
            return -1;
        }
        if (until_eof && left_count == 0) {
            return 0;
        }
        if (!until_eof) {
            length -= left_count;
        }
    } while (until_eof || length != 0);
    return 0;
}

static photoc_jpeg_edit_result
verify_temporary(FILE *source, const char *temporary,
                 const photoc_jpeg_info *original, const photoc_jpeg_exif *exif,
                 const unsigned char *exif_bytes, unsigned int exif_length)
{
    FILE *written = fopen(temporary, "rb");
    if (written == NULL) {
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    photoc_jpeg_info info;
    photoc_jpeg_edit_result result = inspect_file(written, &info);
    if (result == PHOTOC_JPEG_EDIT_OK &&
        (info.width != original->width || info.height != original->height ||
         info.exif_count != 1 || info.exif_after_scan ||
         info.exif_length != (uint64_t)exif_length + 4)) {
        result = PHOTOC_JPEG_EDIT_INVALID_JPEG;
    }
    if (result == PHOTOC_JPEG_EDIT_OK) {
        unsigned char buffer[65533];
        if (fseeko(written, (off_t)(info.exif_offset + 4), SEEK_SET) != 0) {
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        } else if (fread(buffer, 1, exif_length, written) != exif_length) {
            /* A short read at EOF does not set errno. */
            if (!ferror(written)) {
                errno = EIO;
            }
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        } else if (memcmp(buffer, exif_bytes, exif_length) != 0) {
            errno = EIO;
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        }
    }
    if (result == PHOTOC_JPEG_EDIT_OK) {
        uint64_t old_prefix = original->exif_count == 0
                                  ? original->exif_insert_offset
                                  : original->exif_offset;
        uint64_t old_suffix =
            original->exif_count == 0
                ? original->exif_insert_offset
                : original->exif_offset + original->exif_length;
        uint64_t new_suffix = info.exif_offset + info.exif_length;
        if (compare_regions(source, written, 0, 0, old_prefix, false) != 0 ||
            compare_regions(source, written, old_suffix, new_suffix, 0, true) !=
                0) {
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        }
    }
    int saved_errno = errno;
    if (fclose(written) != 0 && result == PHOTOC_JPEG_EDIT_OK) {
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    if (result != PHOTOC_JPEG_EDIT_OK) {
        errno = saved_errno;
        return result;
    }

    photoc_jpeg_exif *reloaded = NULL;
    result = photoc_jpeg_exif_load_copy(temporary, &reloaded);
    ExifContent *reloaded_gps = result == PHOTOC_JPEG_EDIT_OK
                                    ? reloaded->data->ifd[EXIF_IFD_GPS]
                                    : NULL;
    ExifContent *original_gps = exif->data->ifd[EXIF_IFD_GPS];
    if (result == PHOTOC_JPEG_EDIT_OK &&
        (reloaded_gps == NULL || original_gps == NULL ||
         reloaded_gps->count != original_gps->count)) {
        result = PHOTOC_JPEG_EDIT_INVALID_EXIF;
    }
    photoc_jpeg_exif_free(reloaded);
    return result;
}

static photoc_jpeg_edit_result
check_replace_source(const char *path, const photoc_jpeg_exif *exif)
{
    struct stat current;
    if (lstat(path, &current) != 0) {
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    if (!exif->has_source_info || !S_ISREG(current.st_mode) ||
        current.st_nlink != 1 || current.st_uid != geteuid() ||
        (current.st_mode & (S_ISUID | S_ISGID)) != 0 ||
        !same_source(&current, &exif->source_info)) {
        return PHOTOC_JPEG_EDIT_UNSAFE_SOURCE;
    }
    return PHOTOC_JPEG_EDIT_OK;
}

static photoc_jpeg_edit_result write_with_exif(const char *source_path,
                                               const char *destination_path,
                                               const photoc_jpeg_exif *exif,
                                               bool replace_source)
{
    if (source_path == NULL || source_path[0] == '\0' ||
        destination_path == NULL || destination_path[0] == '\0' ||
        destination_path[strlen(destination_path) - 1] == '/' || exif == NULL ||
        exif->data == NULL || exif->memory == NULL) {
        return PHOTOC_JPEG_EDIT_INVALID_ARGUMENT;
    }
    if (replace_source) {
        if (strcmp(source_path, destination_path) != 0) {
            return PHOTOC_JPEG_EDIT_INVALID_ARGUMENT;
        }
        photoc_jpeg_edit_result safety =
            check_replace_source(source_path, exif);
        if (safety != PHOTOC_JPEG_EDIT_OK) {
            return safety;
        }
    } else {
        bool exists;
        if (photoc_fs_exists(destination_path, &exists) != 0) {
            return PHOTOC_JPEG_EDIT_IO_ERROR;
        }
        if (exists) {
            errno = EEXIST;
            return PHOTOC_JPEG_EDIT_IO_ERROR;
        }
    }
    unsigned char *exif_bytes;
    unsigned int exif_length;
    photoc_jpeg_edit_result result =
        serialize_exif(exif, &exif_bytes, &exif_length);
    if (result != PHOTOC_JPEG_EDIT_OK) {
        return result;
    }
    FILE *source = fopen(source_path, "rb");
    if (source == NULL) {
        exif_mem_free(exif->memory, exif_bytes);
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    struct stat source_info;
    photoc_jpeg_info original;
    int saved_errno = 0;
    if (fstat(fileno(source), &source_info) != 0) {
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
    } else if (replace_source &&
               !same_source(&source_info, &exif->source_info)) {
        result = PHOTOC_JPEG_EDIT_UNSAFE_SOURCE;
    } else if (!S_ISREG(source_info.st_mode)) {
        errno = S_ISDIR(source_info.st_mode) ? EISDIR : EINVAL;
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
    } else {
        result = inspect_file(source, &original);
        if (result == PHOTOC_JPEG_EDIT_OK &&
            (original.exif_count > 1 || original.exif_after_scan)) {
            result = PHOTOC_JPEG_EDIT_UNSAFE_LAYOUT;
        }
    }
    if (result == PHOTOC_JPEG_EDIT_IO_ERROR) {
        saved_errno = errno;
    }

    char *parent = NULL;
    char *temporary = NULL;
    FILE *output = NULL;
    bool temporary_created = false;
    if (result == PHOTOC_JPEG_EDIT_OK) {
        result = destination_directory(destination_path, &parent);
    }
    if (result == PHOTOC_JPEG_EDIT_OK &&
        photoc_fs_join(parent, ".photoc-exif-XXXXXX", &temporary) != 0) {
        result = errno == ENOMEM ? PHOTOC_JPEG_EDIT_NO_MEMORY
                                 : PHOTOC_JPEG_EDIT_IO_ERROR;
        saved_errno = errno;
    }
    if (result == PHOTOC_JPEG_EDIT_OK) {
        int descriptor = mkstemp(temporary);
        if (descriptor < 0) {
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
            saved_errno = errno;
        } else {
            temporary_created = true;
            output = fdopen(descriptor, "wb");
            if (output == NULL) {
                saved_errno = errno;
                close(descriptor);
                result = PHOTOC_JPEG_EDIT_IO_ERROR;
            }
        }
    }
    if (result == PHOTOC_JPEG_EDIT_OK &&
        write_jpeg(source, output, &original, exif_bytes, exif_length) != 0) {
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
        saved_errno = errno;
    }
    if (output != NULL) {
        if (result == PHOTOC_JPEG_EDIT_OK && replace_source &&
            fchown(fileno(output), (uid_t)-1, source_info.st_gid) != 0) {
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
            saved_errno = errno;
        }
        if (result == PHOTOC_JPEG_EDIT_OK && replace_source &&
            fchmod(fileno(output), source_info.st_mode & 07777) != 0) {
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
            saved_errno = errno;
        }
        if (result == PHOTOC_JPEG_EDIT_OK &&
            (fflush(output) != 0 || fsync(fileno(output)) != 0)) {
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
            saved_errno = errno;
        }
        if (fclose(output) != 0 && result == PHOTOC_JPEG_EDIT_OK) {
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
            saved_errno = errno;
        }
    }
    if (result == PHOTOC_JPEG_EDIT_OK) {
        result = verify_temporary(source, temporary, &original, exif,
                                  exif_bytes, exif_length);
        if (result == PHOTOC_JPEG_EDIT_IO_ERROR) {
            saved_errno = errno;
        }
    }
    if (result == PHOTOC_JPEG_EDIT_OK && replace_source) {
        struct stat after;
        if (fstat(fileno(source), &after) != 0) {
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
            saved_errno = errno;
        } else if (!same_source(&source_info, &after)) {
            result = PHOTOC_JPEG_EDIT_UNSAFE_SOURCE;
        }
    }
    if (fclose(source) != 0 && result == PHOTOC_JPEG_EDIT_OK) {
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
        saved_errno = errno;
    }
    if (result == PHOTOC_JPEG_EDIT_OK) {
        if (replace_source) {
            result = check_replace_source(source_path, exif);
            if (result == PHOTOC_JPEG_EDIT_IO_ERROR) {
                saved_errno = errno;
            }
        }
        if (result == PHOTOC_JPEG_EDIT_OK &&
            (replace_source ? rename(temporary, destination_path)
                            : photoc_fs_rename_noreplace(
                                  temporary, destination_path)) != 0) {
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
            saved_errno = errno;
        } else if (result == PHOTOC_JPEG_EDIT_OK) {
            temporary_created = false;
        }
    }
    if (temporary_created) {
        unlink(temporary);
    }
    exif_mem_free(exif->memory, exif_bytes);
    free(temporary);
    free(parent);
    if (result == PHOTOC_JPEG_EDIT_IO_ERROR) {
        errno = saved_errno;
    }
    return result;
}

photoc_jpeg_edit_result
photoc_jpeg_write_with_exif(const char *source_path,
                            const char *destination_path,
                            const photoc_jpeg_exif *exif)
{
    return write_with_exif(source_path, destination_path, exif, false);
}

photoc_jpeg_edit_result
photoc_jpeg_replace_with_exif(const char *source_path,
                              const photoc_jpeg_exif *exif)
{
    return write_with_exif(source_path, source_path, exif, true);
}

typedef struct {
    uint64_t offset;
    uint64_t original_length;
    unsigned char *replacement; /* Owned, including marker and length field. */
    size_t replacement_length;
} scrub_edit;

typedef struct {
    FILE *source; /* Borrowed during JPEG inspection. */
    photoc_scrub_mode mode;
    photoc_scrub_report *report; /* Borrowed. */
    scrub_edit *edits;
    size_t count;
    size_t capacity;
    size_t replacement_bytes;
    photoc_jpeg_edit_result result;
    unsigned int exif_orientation;
    unsigned int xmp_orientation;
    bool extended_xmp;
    bool mpf;
} scrub_context;

static unsigned char *minimal_orientation_exif(unsigned int orientation)
{
    unsigned char bytes[36] = {0xff, 0xe1, 0,    34,   'E', 'x', 'i', 'f', 0,
                               0,    'I',  'I',  42,   0,   8,   0,   0,   0,
                               1,    0,    0x12, 0x01, 3,   0,   1,   0,   0,
                               0,    0,    0,    0,    0,   0,   0,   0,   0};
    bytes[28] = (unsigned char)orientation;
    unsigned char *copy = malloc(sizeof(bytes));
    if (copy != NULL)
        memcpy(copy, bytes, sizeof(bytes));
    return copy;
}

static bool xmp_orientation_name(const xmlNs *ns, const xmlChar *name)
{
    return ns != NULL && ns->href != NULL && name != NULL &&
           strcmp((const char *)ns->href, "http://ns.adobe.com/tiff/1.0/") ==
               0 &&
           strcmp((const char *)name, "Orientation") == 0;
}

static bool parse_orientation_text(const xmlChar *value, unsigned int *out)
{
    if (value == NULL)
        return false;
    const unsigned char *cursor = value;
    while (isspace(*cursor))
        ++cursor;
    if (*cursor < '1' || *cursor > '8')
        return false;
    unsigned int parsed = (unsigned int)(*cursor++ - '0');
    while (isspace(*cursor))
        ++cursor;
    if (*cursor != '\0')
        return false;
    *out = parsed;
    return true;
}

static bool collect_xmp_orientation(xmlNode *node, xmlDoc *doc,
                                    unsigned int *orientation)
{
    if (node->type == XML_ELEMENT_NODE &&
        xmp_orientation_name(node->ns, node->name)) {
        xmlChar *value = xmlNodeGetContent(node);
        unsigned int parsed = 0;
        bool valid = parse_orientation_text(value, &parsed) &&
                     (*orientation == 0 || *orientation == parsed);
        xmlFree(value);
        if (!valid)
            return false;
        *orientation = parsed;
    }
    for (xmlAttr *attr = node->properties; attr != NULL; attr = attr->next) {
        if (!xmp_orientation_name(attr->ns, attr->name))
            continue;
        xmlChar *value = xmlNodeListGetString(doc, attr->children, 1);
        unsigned int parsed = 0;
        bool valid = parse_orientation_text(value, &parsed) &&
                     (*orientation == 0 || *orientation == parsed);
        xmlFree(value);
        if (!valid)
            return false;
        *orientation = parsed;
    }
    for (xmlNode *child = node->children; child != NULL; child = child->next) {
        if (!collect_xmp_orientation(child, doc, orientation))
            return false;
    }
    return true;
}

static photoc_jpeg_edit_result
read_xmp_orientation(const unsigned char *payload, unsigned int length,
                     unsigned int *orientation)
{
    const size_t prefix = sizeof("http://ns.adobe.com/xap/1.0/");
    if (length <= prefix)
        return PHOTOC_JPEG_EDIT_UNSUPPORTED_XMP;
    xmlDocPtr doc = xmlReadMemory(
        (const char *)payload + prefix, (int)(length - prefix), "embedded.xmp",
        NULL, XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (doc == NULL)
        return PHOTOC_JPEG_EDIT_UNSUPPORTED_XMP;
    xmlNode *root = xmlDocGetRootElement(doc);
    bool valid = doc->intSubset == NULL && doc->extSubset == NULL &&
                 root != NULL &&
                 collect_xmp_orientation(root, doc, orientation);
    xmlFreeDoc(doc);
    return valid ? PHOTOC_JPEG_EDIT_OK : PHOTOC_JPEG_EDIT_UNSUPPORTED_XMP;
}

static bool xmp_private_name(const xmlNs *ns, const xmlChar *name)
{
    if (ns == NULL || ns->href == NULL || name == NULL)
        return false;
    const char *uri = (const char *)ns->href;
    const char *local = (const char *)name;
    if (strcmp(uri, "http://ns.adobe.com/exif/1.0/") == 0)
        return strncmp(local, "GPS", 3) == 0 ||
               strcmp(local, "BodySerialNumber") == 0 ||
               strcmp(local, "CameraSerialNumber") == 0 ||
               strcmp(local, "LensSerialNumber") == 0 ||
               strcmp(local, "ImageUniqueID") == 0;
    if (strcmp(uri, "http://ns.adobe.com/exif/1.0/aux/") == 0 ||
        strcmp(uri, "http://cipa.jp/exif/1.0/") == 0)
        return strstr(local, "SerialNumber") != NULL ||
               strcmp(local, "ImageUniqueID") == 0;
    if (strcmp(uri, "http://ns.adobe.com/tiff/1.0/") == 0)
        return strcmp(local, "Artist") == 0 ||
               strcmp(local, "ImageUniqueID") == 0;
    if (strcmp(uri, "http://ns.adobe.com/photoshop/1.0/") == 0)
        return strcmp(local, "City") == 0 || strcmp(local, "State") == 0 ||
               strcmp(local, "Country") == 0 ||
               strcmp(local, "Location") == 0 || strcmp(local, "Credit") == 0 ||
               strcmp(local, "CaptionWriter") == 0 ||
               strcmp(local, "Source") == 0;
    if (strcmp(uri, "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/") == 0 ||
        strcmp(uri, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/") == 0)
        return strstr(local, "Location") != NULL ||
               strcmp(local, "City") == 0 ||
               strcmp(local, "ProvinceState") == 0 ||
               strcmp(local, "CountryName") == 0 ||
               strcmp(local, "CountryCode") == 0 ||
               strcmp(local, "Creator") == 0 ||
               strcmp(local, "CreatorContactInfo") == 0 ||
               strstr(local, "SerialNumber") != NULL;
    if (strcmp(uri, "http://purl.org/dc/elements/1.1/") == 0)
        return strcmp(local, "creator") == 0;
    if (strcmp(uri, "http://ns.adobe.com/xap/1.0/rights/") == 0)
        return strcmp(local, "Owner") == 0;
    if (strcmp(uri, "http://ns.adobe.com/xap/1.0/mm/") == 0)
        return strcmp(local, "DocumentID") == 0 ||
               strcmp(local, "InstanceID") == 0 ||
               strcmp(local, "OriginalDocumentID") == 0;
    return false;
}

static bool remove_xmp_private(xmlNode *node)
{
    bool changed = false;
    for (xmlAttr *attr = node->properties; attr != NULL;) {
        xmlAttr *following = attr->next;
        if (xmp_private_name(attr->ns, attr->name)) {
            xmlRemoveProp(attr);
            changed = true;
        }
        attr = following;
    }
    for (xmlNode *child = node->children; child != NULL;) {
        xmlNode *next = child->next;
        if (child->type == XML_ELEMENT_NODE &&
            xmp_private_name(child->ns, child->name)) {
            xmlUnlinkNode(child);
            xmlFreeNode(child);
            changed = true;
        } else {
            changed |= remove_xmp_private(child);
        }
        child = next;
    }
    return changed;
}

static photoc_jpeg_edit_result scrub_xmp(const unsigned char *payload,
                                         unsigned int length,
                                         unsigned char **replacement,
                                         size_t *replacement_length,
                                         bool *changed)
{
    static const char signature[] = "http://ns.adobe.com/xap/1.0/";
    size_t prefix = sizeof(signature);
    if (length <= prefix || length - prefix > INT_MAX)
        return PHOTOC_JPEG_EDIT_UNSUPPORTED_XMP;
    xmlDocPtr doc = xmlReadMemory(
        (const char *)payload + prefix, (int)(length - prefix), "embedded.xmp",
        NULL, XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (doc == NULL)
        return PHOTOC_JPEG_EDIT_UNSUPPORTED_XMP;
    if (doc->intSubset != NULL || doc->extSubset != NULL ||
        xmlDocGetRootElement(doc) == NULL) {
        xmlFreeDoc(doc);
        return PHOTOC_JPEG_EDIT_UNSUPPORTED_XMP;
    }
    *changed = remove_xmp_private((xmlNode *)doc);
    if (!*changed) {
        xmlFreeDoc(doc);
        return PHOTOC_JPEG_EDIT_OK;
    }
    xmlChar *xml = NULL;
    int xml_length = 0;
    xmlDocDumpMemoryEnc(doc, &xml, &xml_length, "UTF-8");
    xmlFreeDoc(doc);
    if (xml == NULL || xml_length < 0 || prefix + (size_t)xml_length > 65533) {
        xmlFree(xml);
        return PHOTOC_JPEG_EDIT_UNSUPPORTED_XMP;
    }
    size_t total = 4 + prefix + (size_t)xml_length;
    unsigned char *bytes = malloc(total);
    if (bytes == NULL) {
        xmlFree(xml);
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    }
    bytes[0] = 0xff;
    bytes[1] = 0xe1;
    unsigned int marker_length = (unsigned int)(total - 2);
    bytes[2] = (unsigned char)(marker_length >> 8);
    bytes[3] = (unsigned char)marker_length;
    memcpy(bytes + 4, payload, prefix);
    memcpy(bytes + 4 + prefix, xml, (size_t)xml_length);
    xmlFree(xml);
    *replacement = bytes;
    *replacement_length = total;
    return PHOTOC_JPEG_EDIT_OK;
}

static bool exif_remove_entry(ExifContent *content, ExifTag tag)
{
    if (content == NULL)
        return false;
    ExifEntry *entry = exif_content_get_entry(content, tag);
    if (entry == NULL)
        return false;
    exif_content_remove_entry(content, entry);
    return true;
}

static photoc_jpeg_edit_result
scrub_exif(const unsigned char *payload, unsigned int length,
           photoc_scrub_mode mode, photoc_scrub_report *report,
           unsigned char **replacement, size_t *replacement_length,
           bool *changed)
{
    photoc_jpeg_exif *exif = NULL;
    photoc_jpeg_edit_result result =
        parse_exif_segment(payload, length, &exif, true);
    if (result != PHOTOC_JPEG_EDIT_OK)
        return result;
    ExifContent *root = exif->data->ifd[EXIF_IFD_0];
    ExifContent *sub = exif->data->ifd[EXIF_IFD_EXIF];
    report->maker_note =
        sub != NULL && exif_content_get_entry(sub, EXIF_TAG_MAKER_NOTE) != NULL;
    if (mode == PHOTOC_SCRUB_ALL_METADATA) {
        ExifEntry *orientation =
            root == NULL ? NULL
                         : exif_content_get_entry(root, EXIF_TAG_ORIENTATION);
        if (orientation != NULL &&
            (orientation->format != EXIF_FORMAT_SHORT ||
             orientation->components != 1 || orientation->size < 2 ||
             orientation->data == NULL)) {
            result = PHOTOC_JPEG_EDIT_INVALID_EXIF;
        } else if (orientation != NULL) {
            ExifByteOrder order = exif_data_get_byte_order(exif->data);
            unsigned int value = exif_get_short(orientation->data, order);
            if (value < 1 || value > 8) {
                result = PHOTOC_JPEG_EDIT_INVALID_EXIF;
            } else {
                *replacement = minimal_orientation_exif(value);
                if (*replacement == NULL)
                    result = PHOTOC_JPEG_EDIT_NO_MEMORY;
                else
                    *replacement_length = 36;
            }
        }
        report->exif_gps = photoc_jpeg_exif_has_gps(exif);
        report->exif_identifiers = true;
        *changed = result == PHOTOC_JPEG_EDIT_OK;
    } else {
        report->exif_gps = photoc_jpeg_exif_has_gps(exif);
        if (report->exif_gps)
            result = photoc_jpeg_exif_remove_gps(exif);
        if (result == PHOTOC_JPEG_EDIT_OK) {
            bool identifiers = false;
            identifiers |= exif_remove_entry(root, EXIF_TAG_ARTIST);
            identifiers |= exif_remove_entry(root, EXIF_TAG_IMAGE_UNIQUE_ID);
            identifiers |= exif_remove_entry(sub, EXIF_TAG_BODY_SERIAL_NUMBER);
            identifiers |= exif_remove_entry(sub, EXIF_TAG_CAMERA_OWNER_NAME);
            identifiers |= exif_remove_entry(sub, EXIF_TAG_LENS_SERIAL_NUMBER);
            identifiers |= exif_remove_entry(sub, EXIF_TAG_IMAGE_UNIQUE_ID);
            report->exif_identifiers = identifiers;
            *changed = identifiers || report->exif_gps;
            if (*changed) {
                unsigned char *bytes = NULL;
                unsigned int bytes_length = 0;
                result = serialize_exif(exif, &bytes, &bytes_length);
                if (result == PHOTOC_JPEG_EDIT_OK) {
                    size_t total = (size_t)bytes_length + 4;
                    *replacement = malloc(total);
                    if (*replacement == NULL) {
                        result = PHOTOC_JPEG_EDIT_NO_MEMORY;
                    } else {
                        (*replacement)[0] = 0xff;
                        (*replacement)[1] = 0xe1;
                        unsigned int segment_length = bytes_length + 2;
                        (*replacement)[2] =
                            (unsigned char)(segment_length >> 8);
                        (*replacement)[3] = (unsigned char)segment_length;
                        memcpy(*replacement + 4, bytes, bytes_length);
                        *replacement_length = total;
                    }
                    exif_mem_free(exif->memory, bytes);
                }
            }
        }
    }
    photoc_jpeg_exif_free(exif);
    return result;
}

static bool iptc_private_dataset(unsigned int record, unsigned int dataset)
{
    if (record != 2)
        return false;
    switch (dataset) {
    case 80:  /* By-line */
    case 85:  /* By-line title */
    case 90:  /* City */
    case 92:  /* Sublocation */
    case 95:  /* Province/state */
    case 100: /* Country code */
    case 101: /* Country name */
    case 110: /* Credit */
    case 115: /* Source */
    case 118: /* Contact */
    case 122: /* Caption writer */
        return true;
    default:
        return false;
    }
}

static photoc_jpeg_edit_result scrub_iptc(const unsigned char *payload,
                                          unsigned int length,
                                          unsigned char **replacement,
                                          size_t *replacement_length,
                                          bool *changed, bool *unsupported)
{
    static const unsigned char signature[] = "Photoshop 3.0\0";
    if (length < sizeof(signature) - 1 ||
        memcmp(payload, signature, sizeof(signature) - 1) != 0) {
        *unsupported = true;
        return PHOTOC_JPEG_EDIT_OK;
    }
    unsigned char *output = malloc((size_t)length + 4);
    if (output == NULL)
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    size_t input_at = sizeof(signature) - 1;
    size_t output_at = 4 + input_at;
    memcpy(output + 4, payload, input_at);
    photoc_jpeg_edit_result result = PHOTOC_JPEG_EDIT_OK;
    while (input_at < length) {
        size_t start = input_at;
        if (length - input_at < 11 ||
            memcmp(payload + input_at, "8BIM", 4) != 0) {
            result = PHOTOC_JPEG_EDIT_UNSUPPORTED_IPTC;
            break;
        }
        unsigned int id =
            (unsigned int)payload[input_at + 4] << 8 | payload[input_at + 5];
        input_at += 6;
        size_t name_length = (size_t)payload[input_at] + 1;
        size_t name_padded = name_length + (name_length & 1);
        if (name_padded > length - input_at ||
            length - input_at - name_padded < 4) {
            result = PHOTOC_JPEG_EDIT_UNSUPPORTED_IPTC;
            break;
        }
        input_at += name_padded;
        size_t size_at = input_at;
        uint32_t data_length = (uint32_t)payload[input_at] << 24 |
                               (uint32_t)payload[input_at + 1] << 16 |
                               (uint32_t)payload[input_at + 2] << 8 |
                               payload[input_at + 3];
        input_at += 4;
        if (data_length > length - input_at ||
            (data_length & 1u) > length - input_at - data_length) {
            result = PHOTOC_JPEG_EDIT_UNSUPPORTED_IPTC;
            break;
        }
        size_t end = input_at + data_length + (data_length & 1u);
        if (id != 0x0404) {
            memcpy(output + output_at, payload + start, end - start);
            output_at += end - start;
        } else {
            size_t header = size_at + 4 - start;
            memcpy(output + output_at, payload + start, header);
            size_t new_size_at = output_at + header - 4;
            output_at += header;
            size_t dataset_at = input_at;
            while (dataset_at < input_at + data_length) {
                if (input_at + data_length - dataset_at < 5 ||
                    payload[dataset_at] != 0x1c) {
                    result = PHOTOC_JPEG_EDIT_UNSUPPORTED_IPTC;
                    break;
                }
                unsigned int record = payload[dataset_at + 1];
                unsigned int dataset = payload[dataset_at + 2];
                unsigned int value_length =
                    (unsigned int)payload[dataset_at + 3] << 8 |
                    payload[dataset_at + 4];
                if ((value_length & 0x8000u) != 0 ||
                    value_length > input_at + data_length - dataset_at - 5) {
                    result = PHOTOC_JPEG_EDIT_UNSUPPORTED_IPTC;
                    break;
                }
                size_t field_length = (size_t)value_length + 5;
                if (iptc_private_dataset(record, dataset)) {
                    *changed = true;
                } else {
                    memcpy(output + output_at, payload + dataset_at,
                           field_length);
                    output_at += field_length;
                }
                dataset_at += field_length;
            }
            if (result != PHOTOC_JPEG_EDIT_OK)
                break;
            size_t new_length = output_at - new_size_at - 4;
            output[new_size_at] = (unsigned char)(new_length >> 24);
            output[new_size_at + 1] = (unsigned char)(new_length >> 16);
            output[new_size_at + 2] = (unsigned char)(new_length >> 8);
            output[new_size_at + 3] = (unsigned char)new_length;
            if ((new_length & 1u) != 0)
                output[output_at++] = 0;
        }
        input_at = end;
    }
    if (result == PHOTOC_JPEG_EDIT_OK && *changed) {
        if (output_at > 65537) {
            result = PHOTOC_JPEG_EDIT_UNSUPPORTED_IPTC;
        } else {
            unsigned int segment_length = (unsigned int)output_at - 2;
            output[0] = 0xff;
            output[1] = 0xed;
            output[2] = (unsigned char)(segment_length >> 8);
            output[3] = (unsigned char)segment_length;
            *replacement = output;
            *replacement_length = output_at;
            return PHOTOC_JPEG_EDIT_OK;
        }
    }
    free(output);
    return result;
}

static photoc_jpeg_edit_result scrub_append(scrub_context *ctx, uint64_t offset,
                                            uint64_t original_length,
                                            unsigned char *replacement,
                                            size_t replacement_length)
{
    if (ctx->count >= 65536 ||
        replacement_length > (size_t)67108864 - ctx->replacement_bytes)
        return PHOTOC_JPEG_EDIT_METADATA_TOO_LARGE;
    if (ctx->count == ctx->capacity) {
        size_t next = ctx->capacity == 0 ? 8 : ctx->capacity * 2;
        if (next < ctx->capacity || next > SIZE_MAX / sizeof(*ctx->edits))
            return PHOTOC_JPEG_EDIT_NO_MEMORY;
        scrub_edit *grown = realloc(ctx->edits, next * sizeof(*ctx->edits));
        if (grown == NULL)
            return PHOTOC_JPEG_EDIT_NO_MEMORY;
        ctx->edits = grown;
        ctx->capacity = next;
    }
    ctx->edits[ctx->count++] =
        (scrub_edit){offset, original_length, replacement, replacement_length};
    ctx->replacement_bytes += replacement_length;
    ctx->report->changed = true;
    return PHOTOC_JPEG_EDIT_OK;
}

static int scrub_visit(unsigned char marker, const unsigned char *payload,
                       unsigned int length, bool after_scan, void *user_data)
{
    scrub_context *ctx = user_data;
    off_t end = ftello(ctx->source);
    if (end < 0 || (uint64_t)end < (uint64_t)length + 4) {
        ctx->result = PHOTOC_JPEG_EDIT_IO_ERROR;
        return -1;
    }
    uint64_t offset = (uint64_t)end - length - 4;
    unsigned char *replacement = NULL;
    size_t replacement_length = 0;
    bool changed = false;
    if (marker == 0xe1 && length >= 6 && memcmp(payload, "Exif\0\0", 6) == 0) {
        ctx->result = scrub_exif(payload, length, ctx->mode, ctx->report,
                                 &replacement, &replacement_length, &changed);
        if (ctx->mode == PHOTOC_SCRUB_ALL_METADATA && replacement_length == 36)
            ctx->exif_orientation = replacement[28];
    } else if (marker == 0xe1 && length >= 29 &&
               memcmp(payload, "http://ns.adobe.com/xap/1.0/\0", 29) == 0) {
        if (ctx->mode == PHOTOC_SCRUB_ALL_METADATA) {
            ctx->result =
                read_xmp_orientation(payload, length, &ctx->xmp_orientation);
            if (ctx->result == PHOTOC_JPEG_EDIT_OK) {
                changed = true;
                ctx->report->xmp_fields = true;
            }
        } else {
            ctx->result = scrub_xmp(payload, length, &replacement,
                                    &replacement_length, &changed);
            ctx->report->xmp_fields |= changed;
            ctx->report->unhandled_metadata = true;
        }
    } else if (marker == 0xe1 && length >= 35 &&
               memcmp(payload, "http://ns.adobe.com/xmp/extension/\0", 35) ==
                   0) {
        ctx->extended_xmp = true;
        if (ctx->mode == PHOTOC_SCRUB_ALL_METADATA) {
            changed = true;
            ctx->report->xmp_fields = true;
        } else {
            ctx->result = PHOTOC_JPEG_EDIT_UNSUPPORTED_XMP;
        }
    } else if (marker == 0xed) {
        if (ctx->mode == PHOTOC_SCRUB_ALL_METADATA) {
            changed = true;
            ctx->report->iptc_fields = true;
        } else {
            ctx->result =
                scrub_iptc(payload, length, &replacement, &replacement_length,
                           &changed, &ctx->report->unhandled_metadata);
            ctx->report->iptc_fields |= changed;
            ctx->report->unhandled_metadata = true;
        }
    } else if (marker == 0xfe && ctx->mode == PHOTOC_SCRUB_ALL_METADATA) {
        changed = true;
    } else if (marker == 0xe2 && length >= 4 &&
               memcmp(payload, "MPF\0", 4) == 0) {
        ctx->mpf = true;
        ctx->report->unhandled_metadata = true;
    } else if ((marker >= 0xe0 && marker <= 0xef) && marker != 0xe0 &&
               marker != 0xee) {
        /* ICC APP2 is retained unchanged; other opaque APP data is unknown. */
        if (!(marker == 0xe2 && length >= 12 &&
              memcmp(payload, "ICC_PROFILE\0", 12) == 0))
            ctx->report->unhandled_metadata = true;
    }
    if (ctx->result == PHOTOC_JPEG_EDIT_OK && changed && after_scan)
        ctx->result = PHOTOC_JPEG_EDIT_UNSAFE_METADATA_LAYOUT;
    if (ctx->result == PHOTOC_JPEG_EDIT_OK && changed)
        ctx->result = scrub_append(ctx, offset, (uint64_t)length + 4,
                                   replacement, replacement_length);
    if (ctx->result != PHOTOC_JPEG_EDIT_OK) {
        free(replacement);
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int scrub_edit_order(const void *left, const void *right)
{
    const scrub_edit *a = left;
    const scrub_edit *b = right;
    if (a->offset < b->offset)
        return -1;
    if (a->offset > b->offset)
        return 1;
    if (a->original_length == b->original_length)
        return 0;
    return (a->original_length == 0) ? -1 : (b->original_length == 0) ? 1 : 0;
}

static photoc_jpeg_edit_result scrub_write_stream(FILE *source, FILE *output,
                                                  const scrub_context *ctx,
                                                  uint64_t source_size)
{
    uint64_t cursor = 0;
    for (size_t i = 0; i < ctx->count; ++i) {
        const scrub_edit *edit = &ctx->edits[i];
        if (edit->offset < cursor || edit->offset > source_size ||
            edit->original_length > source_size - edit->offset ||
            fseeko(source, (off_t)cursor, SEEK_SET) != 0 ||
            copy_bytes(source, output, edit->offset - cursor) != 0)
            return PHOTOC_JPEG_EDIT_IO_ERROR;
        if (edit->replacement_length != 0 &&
            fwrite(edit->replacement, 1, edit->replacement_length, output) !=
                edit->replacement_length)
            return PHOTOC_JPEG_EDIT_IO_ERROR;
        cursor = edit->offset + edit->original_length;
    }
    if (fseeko(source, (off_t)cursor, SEEK_SET) != 0 ||
        copy_bytes(source, output, source_size - cursor) != 0)
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    return PHOTOC_JPEG_EDIT_OK;
}

static photoc_jpeg_edit_result scrub_verify(FILE *source, const char *temporary,
                                            const scrub_context *ctx,
                                            const photoc_jpeg_info *original,
                                            uint64_t source_size)
{
    FILE *written = fopen(temporary, "rb");
    if (written == NULL)
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    photoc_jpeg_info result_info;
    photoc_jpeg_edit_result result = inspect_file(written, &result_info);
    if (result == PHOTOC_JPEG_EDIT_OK &&
        (result_info.width != original->width ||
         result_info.height != original->height ||
         result_info.components != original->components ||
         result_info.scan_count != original->scan_count))
        result = PHOTOC_JPEG_EDIT_INVALID_JPEG;
    uint64_t original_at = 0;
    uint64_t written_at = 0;
    for (size_t i = 0; i < ctx->count && result == PHOTOC_JPEG_EDIT_OK; ++i) {
        const scrub_edit *edit = &ctx->edits[i];
        if (compare_regions(source, written, original_at, written_at,
                            edit->offset - original_at, false) != 0) {
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
            break;
        }
        written_at += edit->offset - original_at;
        if (edit->replacement_length != 0) {
            unsigned char buffer[65537];
            if (edit->replacement_length > sizeof(buffer) ||
                fseeko(written, (off_t)written_at, SEEK_SET) != 0 ||
                fread(buffer, 1, edit->replacement_length, written) !=
                    edit->replacement_length ||
                memcmp(buffer, edit->replacement, edit->replacement_length) !=
                    0) {
                result = PHOTOC_JPEG_EDIT_IO_ERROR;
                break;
            }
        }
        written_at += edit->replacement_length;
        original_at = edit->offset + edit->original_length;
    }
    if (result == PHOTOC_JPEG_EDIT_OK &&
        compare_regions(source, written, original_at, written_at,
                        source_size - original_at, false) != 0)
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
    if (result == PHOTOC_JPEG_EDIT_OK) {
        if (fseeko(written, 0, SEEK_END) != 0)
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        else {
            off_t end = ftello(written);
            if (end < 0 ||
                (uint64_t)end != written_at + source_size - original_at)
                result = PHOTOC_JPEG_EDIT_IO_ERROR;
        }
    }
    int saved_errno = errno;
    if (fclose(written) != 0 && result == PHOTOC_JPEG_EDIT_OK)
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
    if (result != PHOTOC_JPEG_EDIT_OK)
        errno = saved_errno == 0 ? EIO : saved_errno;
    return result;
}

static photoc_jpeg_edit_result scrub_source_safe(const char *path,
                                                 const struct stat *snapshot)
{
    struct stat current;
    if (lstat(path, &current) != 0)
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    if (!S_ISREG(current.st_mode) || current.st_nlink != 1 ||
        current.st_uid != geteuid() ||
        (current.st_mode & (S_ISUID | S_ISGID)) != 0 ||
        !same_source(&current, snapshot))
        return PHOTOC_JPEG_EDIT_UNSAFE_SOURCE;
    return PHOTOC_JPEG_EDIT_OK;
}

photoc_jpeg_edit_result photoc_jpeg_scrub_metadata(const char *source_path,
                                                   const char *destination_path,
                                                   photoc_scrub_mode mode,
                                                   bool in_place,
                                                   photoc_scrub_report *report)
{
    if (source_path == NULL || destination_path == NULL || report == NULL ||
        source_path[0] == '\0' || destination_path[0] == '\0' ||
        (mode != PHOTOC_SCRUB_PRIVACY && mode != PHOTOC_SCRUB_ALL_METADATA) ||
        (in_place && strcmp(source_path, destination_path) != 0) ||
        (!in_place && strcmp(source_path, destination_path) == 0))
        return PHOTOC_JPEG_EDIT_INVALID_ARGUMENT;
    *report = (photoc_scrub_report){0};
    FILE *source = fopen(source_path, "rb");
    if (source == NULL)
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    photoc_jpeg_edit_result result = PHOTOC_JPEG_EDIT_OK;
    struct stat snapshot;
    photoc_jpeg_info original;
    scrub_context ctx = {.source = source,
                         .mode = mode,
                         .report = report,
                         .result = PHOTOC_JPEG_EDIT_OK};
    if (fstat(fileno(source), &snapshot) != 0)
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
    else if (!S_ISREG(snapshot.st_mode) || snapshot.st_size < 0)
        result = PHOTOC_JPEG_EDIT_UNSAFE_SOURCE;
    else if (in_place)
        result = scrub_source_safe(source_path, &snapshot);
    if (result == PHOTOC_JPEG_EDIT_OK) {
        if (photoc_jpeg_inspect_app_limited(source, &original, scrub_visit,
                                            &ctx,
                                            (uint64_t)snapshot.st_size) != 0)
            result = ctx.result != PHOTOC_JPEG_EDIT_OK ? ctx.result
                     : errno == EINVAL ? PHOTOC_JPEG_EDIT_INVALID_JPEG
                                       : PHOTOC_JPEG_EDIT_IO_ERROR;
        else if (original.exif_count > 1 || original.exif_after_scan)
            result = PHOTOC_JPEG_EDIT_UNSAFE_LAYOUT;
    }
    if (result == PHOTOC_JPEG_EDIT_OK && mode == PHOTOC_SCRUB_ALL_METADATA &&
        ctx.extended_xmp && ctx.exif_orientation == 0 &&
        ctx.xmp_orientation == 0)
        result = PHOTOC_JPEG_EDIT_UNSUPPORTED_XMP;
    if (result == PHOTOC_JPEG_EDIT_OK && ctx.mpf && ctx.count != 0)
        result = PHOTOC_JPEG_EDIT_UNSAFE_METADATA_LAYOUT;
    if (result == PHOTOC_JPEG_EDIT_OK && mode == PHOTOC_SCRUB_ALL_METADATA &&
        ctx.xmp_orientation != 0 && ctx.exif_orientation == 0) {
        unsigned char *orientation =
            minimal_orientation_exif(ctx.xmp_orientation);
        if (orientation == NULL) {
            result = PHOTOC_JPEG_EDIT_NO_MEMORY;
        } else if (original.exif_count == 1) {
            for (size_t i = 0; i < ctx.count; ++i) {
                if (ctx.edits[i].offset == original.exif_offset) {
                    ctx.edits[i].replacement = orientation;
                    ctx.edits[i].replacement_length = 36;
                    orientation = NULL;
                    break;
                }
            }
            free(orientation);
        } else {
            result = scrub_append(&ctx, original.exif_insert_offset, 0,
                                  orientation, 36);
            if (result != PHOTOC_JPEG_EDIT_OK)
                free(orientation);
            else
                qsort(ctx.edits, ctx.count, sizeof(*ctx.edits),
                      scrub_edit_order);
        }
    }
    if (result == PHOTOC_JPEG_EDIT_OK && ctx.count == 0) {
        struct stat after;
        if (fstat(fileno(source), &after) != 0)
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        else if (!same_source(&snapshot, &after))
            result = PHOTOC_JPEG_EDIT_UNSAFE_SOURCE;
    }
    if (result == PHOTOC_JPEG_EDIT_OK && ctx.count != 0 && !in_place) {
        bool exists;
        if (photoc_fs_exists(destination_path, &exists) != 0)
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        else if (exists) {
            errno = EEXIST;
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        }
    }
    char *parent = NULL;
    char *temporary = NULL;
    bool created = false;
    FILE *output = NULL;
    int saved_errno = errno;
    if (result == PHOTOC_JPEG_EDIT_OK && ctx.count != 0)
        result = destination_directory(destination_path, &parent);
    if (result == PHOTOC_JPEG_EDIT_OK && ctx.count != 0 &&
        photoc_fs_join(parent, ".photoc-scrub-XXXXXX", &temporary) != 0)
        result = errno == ENOMEM ? PHOTOC_JPEG_EDIT_NO_MEMORY
                                 : PHOTOC_JPEG_EDIT_IO_ERROR;
    if (result == PHOTOC_JPEG_EDIT_OK && ctx.count != 0) {
        int fd = mkstemp(temporary);
        if (fd < 0)
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        else {
            created = true;
            output = fdopen(fd, "wb");
            if (output == NULL) {
                close(fd);
                result = PHOTOC_JPEG_EDIT_IO_ERROR;
            }
        }
    }
    if (result == PHOTOC_JPEG_EDIT_OK && output != NULL)
        result = scrub_write_stream(source, output, &ctx,
                                    (uint64_t)snapshot.st_size);
    if (output != NULL) {
        if (result == PHOTOC_JPEG_EDIT_OK && in_place &&
            (fchown(fileno(output), (uid_t)-1, snapshot.st_gid) != 0 ||
             fchmod(fileno(output), snapshot.st_mode & 07777) != 0))
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        if (result == PHOTOC_JPEG_EDIT_OK &&
            (fflush(output) != 0 || fsync(fileno(output)) != 0))
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        if (fclose(output) != 0 && result == PHOTOC_JPEG_EDIT_OK)
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    if (result == PHOTOC_JPEG_EDIT_OK && created)
        result = scrub_verify(source, temporary, &ctx, &original,
                              (uint64_t)snapshot.st_size);
    if (result == PHOTOC_JPEG_EDIT_OK && created) {
        struct stat after;
        if (fstat(fileno(source), &after) != 0)
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        else if (!same_source(&snapshot, &after))
            result = PHOTOC_JPEG_EDIT_UNSAFE_SOURCE;
    }
    if (result != PHOTOC_JPEG_EDIT_OK)
        saved_errno = errno;
    if (fclose(source) != 0 && result == PHOTOC_JPEG_EDIT_OK) {
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
        saved_errno = errno;
    }
    if (result == PHOTOC_JPEG_EDIT_OK && created) {
        if (in_place)
            result = scrub_source_safe(source_path, &snapshot);
        if (result == PHOTOC_JPEG_EDIT_OK &&
            (in_place ? rename(temporary, destination_path)
                      : photoc_fs_rename_noreplace(temporary,
                                                   destination_path)) != 0)
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        if (result == PHOTOC_JPEG_EDIT_OK)
            created = false;
        else
            saved_errno = errno;
    }
    if (created)
        unlink(temporary);
    for (size_t i = 0; i < ctx.count; ++i)
        free(ctx.edits[i].replacement);
    free(ctx.edits);
    free(parent);
    free(temporary);
    if (result != PHOTOC_JPEG_EDIT_OK)
        *report = (photoc_scrub_report){0};
    if (result == PHOTOC_JPEG_EDIT_IO_ERROR)
        errno = saved_errno;
    return result;
}

static photoc_jpeg_edit_result
write_encoded(const char *destination_path, const photoc_jpeg_buffer *encoded,
              const photoc_jpeg_exif *exif,
              const photoc_jpeg_metadata *metadata)
{
    if (destination_path == NULL || destination_path[0] == '\0' ||
        destination_path[strlen(destination_path) - 1] == '/' ||
        encoded == NULL || encoded->data == NULL || encoded->size == 0 ||
        (exif != NULL && (exif->data == NULL || exif->memory == NULL))) {
        return PHOTOC_JPEG_EDIT_INVALID_ARGUMENT;
    }
    bool exists;
    if (photoc_fs_exists(destination_path, &exists) != 0) {
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    if (exists) {
        errno = EEXIST;
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    static const char suffix[] = ".photoc-XXXXXX";
    size_t length = strlen(destination_path);
    if (length > SIZE_MAX - sizeof(suffix)) {
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    }
    char *temporary = malloc(length + sizeof(suffix));
    if (temporary == NULL) {
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    }
    memcpy(temporary, destination_path, length);
    memcpy(temporary + length, suffix, sizeof(suffix));

    photoc_jpeg_edit_result result = PHOTOC_JPEG_EDIT_OK;
    int saved_errno = 0;
    bool temporary_created = false;
    int descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
        saved_errno = errno;
        goto cleanup;
    }
    temporary_created = true;
    FILE *file = fdopen(descriptor, "wb");
    if (file == NULL) {
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
        saved_errno = errno;
        close(descriptor);
        goto cleanup;
    }
    errno = 0;
    if (metadata != NULL) {
        result = photoc_jpeg_metadata_write_encoded(file, encoded, metadata);
    } else if (fwrite(encoded->data, 1, encoded->size, file) != encoded->size) {
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    if (result == PHOTOC_JPEG_EDIT_IO_ERROR) {
        saved_errno = errno == 0 ? EIO : errno;
    }
    if (result == PHOTOC_JPEG_EDIT_OK &&
        (fflush(file) != 0 || fsync(fileno(file)) != 0)) {
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
        saved_errno = errno == 0 ? EIO : errno;
    }
    if (fclose(file) != 0 && result == PHOTOC_JPEG_EDIT_OK) {
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
        saved_errno = errno;
    }
    if (result != PHOTOC_JPEG_EDIT_OK) {
        goto cleanup;
    }

    FILE *verify = fopen(temporary, "rb");
    if (verify == NULL) {
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
        saved_errno = errno;
        goto cleanup;
    }
    photoc_jpeg_info info;
    result = inspect_file(verify, &info);
    if (result == PHOTOC_JPEG_EDIT_IO_ERROR) {
        saved_errno = errno;
    }
    if (fclose(verify) != 0 && result == PHOTOC_JPEG_EDIT_OK) {
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
        saved_errno = errno;
    }
    if (result != PHOTOC_JPEG_EDIT_OK) {
        goto cleanup;
    }
    if (metadata != NULL) {
        result = photoc_jpeg_metadata_verify(metadata, temporary);
        if (result != PHOTOC_JPEG_EDIT_OK) {
            if (result == PHOTOC_JPEG_EDIT_IO_ERROR) {
                saved_errno = errno;
            }
            goto cleanup;
        }
    }
    if (exif != NULL) {
        result = photoc_jpeg_write_with_exif(temporary, destination_path, exif);
    } else if (photoc_fs_rename_noreplace(temporary, destination_path) != 0) {
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
    } else {
        temporary_created = false;
    }
    if (result == PHOTOC_JPEG_EDIT_IO_ERROR) {
        saved_errno = errno;
    }

cleanup:
    if (temporary_created) {
        unlink(temporary);
    }
    free(temporary);
    if (result == PHOTOC_JPEG_EDIT_IO_ERROR) {
        errno = saved_errno;
    }
    return result;
}

photoc_jpeg_edit_result
photoc_jpeg_write_encoded(const char *destination_path,
                          const photoc_jpeg_buffer *encoded,
                          const photoc_jpeg_exif *exif)
{
    return write_encoded(destination_path, encoded, exif, NULL);
}

photoc_jpeg_edit_result
photoc_jpeg_write_encoded_with_metadata(const char *destination_path,
                                        const photoc_jpeg_buffer *encoded,
                                        const photoc_jpeg_metadata *metadata)
{
    if (metadata == NULL) {
        return PHOTOC_JPEG_EDIT_INVALID_ARGUMENT;
    }
    return write_encoded(destination_path, encoded, NULL, metadata);
}

const char *photoc_jpeg_edit_result_message(photoc_jpeg_edit_result result)
{
    switch (result) {
    case PHOTOC_JPEG_EDIT_OK:
        return "JPEG written";
    case PHOTOC_JPEG_EDIT_INVALID_ARGUMENT:
        return "invalid argument";
    case PHOTOC_JPEG_EDIT_INVALID_JPEG:
        return "invalid JPEG";
    case PHOTOC_JPEG_EDIT_NO_EXIF:
        return "JPEG has no EXIF";
    case PHOTOC_JPEG_EDIT_INVALID_EXIF:
        return "invalid EXIF";
    case PHOTOC_JPEG_EDIT_UNSAFE_LAYOUT:
        return "ambiguous JPEG EXIF layout";
    case PHOTOC_JPEG_EDIT_EXIF_TOO_LARGE:
        return "EXIF exceeds JPEG APP1 limit";
    case PHOTOC_JPEG_EDIT_IO_ERROR:
        return "JPEG I/O error";
    case PHOTOC_JPEG_EDIT_NO_MEMORY:
        return "out of memory";
    case PHOTOC_JPEG_EDIT_INVALID_ICC:
        return "invalid ICC chunk sequence/count or empty profile";
    case PHOTOC_JPEG_EDIT_METADATA_TOO_LARGE:
        return "JPEG metadata exceeds 64 MiB snapshot limit";
    case PHOTOC_JPEG_EDIT_UNSAFE_METADATA_LAYOUT:
        return "unsupported or changed JPEG metadata layout";
    case PHOTOC_JPEG_EDIT_UNSAFE_SOURCE:
        return "unsafe in-place source (changed, linked, symlinked, or not "
               "owned by this user)";
    case PHOTOC_JPEG_EDIT_UNSUPPORTED_XMP:
        return "unsupported or malformed XMP; scrub refused";
    case PHOTOC_JPEG_EDIT_UNSUPPORTED_IPTC:
        return "unsupported or malformed IPTC; scrub refused";
    default:
        return "unknown JPEG editing error";
    }
}
