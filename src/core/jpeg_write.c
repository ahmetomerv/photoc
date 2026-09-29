#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/jpeg_write.h"

#include "photoc/fs.h"
#include "jpeg.h"
#include "jpeg_metadata_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <libexif/exif-content.h>
#include <libexif/exif-data.h>
#include <libexif/exif-log.h>
#include <libexif/exif-mem.h>
#include <libexif/exif-utils.h>
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
    default:
        return "unknown JPEG editing error";
    }
}
