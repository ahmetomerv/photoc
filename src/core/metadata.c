#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/photo.h"
#include "jpeg.h"
#include "metadata_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libexif/exif-data.h>
#include <libexif/exif-utils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ExifEntry *entry(ExifData *data, ExifIfd ifd, ExifTag tag)
{
    return exif_content_get_entry(data->ifd[ifd], tag);
}

static bool copy_ascii(ExifEntry *source, char **destination)
{
    if (source == NULL || source->format != EXIF_FORMAT_ASCII ||
        source->data == NULL || source->size == 0) {
        return true;
    }
    size_t length = 0;
    while (length < source->size && source->data[length] != '\0') {
        ++length;
    }
    while (length != 0 && source->data[length - 1] == ' ') {
        --length;
    }
    if (length == 0) {
        return true;
    }
    char *value = malloc(length + 1);
    if (value == NULL) {
        return false;
    }
    memcpy(value, source->data, length);
    value[length] = '\0';
    *destination = value;
    return true;
}

static bool read_rational(ExifEntry *source, ExifByteOrder order, double *value)
{
    if (source == NULL || source->format != EXIF_FORMAT_RATIONAL ||
        source->data == NULL || source->size < 8 || source->components < 1) {
        return false;
    }
    ExifRational rational = exif_get_rational(source->data, order);
    if (rational.denominator == 0) {
        return false;
    }
    *value = (double)rational.numerator / (double)rational.denominator;
    return *value > 0.0;
}

static bool read_iso(ExifEntry *source, ExifByteOrder order, uint32_t *iso)
{
    if (source == NULL || source->data == NULL || source->components < 1) {
        return false;
    }
    if (source->format == EXIF_FORMAT_SHORT && source->size >= 2) {
        *iso = exif_get_short(source->data, order);
    } else if (source->format == EXIF_FORMAT_LONG && source->size >= 4) {
        *iso = exif_get_long(source->data, order);
    } else {
        return false;
    }
    return *iso != 0;
}

static bool read_coordinate(ExifEntry *source, ExifEntry *reference,
                            ExifByteOrder order, bool latitude, double *value)
{
    if (source == NULL || reference == NULL || source->data == NULL ||
        reference->data == NULL || source->format != EXIF_FORMAT_RATIONAL ||
        source->components != 3 || source->size < 24 ||
        reference->format != EXIF_FORMAT_ASCII || reference->size < 2) {
        return false;
    }
    char direction = (char)reference->data[0];
    if (reference->data[1] != '\0' ||
        (latitude ? (direction != 'N' && direction != 'S')
                  : (direction != 'E' && direction != 'W'))) {
        return false;
    }
    double parts[3];
    for (size_t i = 0; i < 3; ++i) {
        ExifRational part = exif_get_rational(source->data + i * 8, order);
        if (part.denominator == 0) {
            return false;
        }
        parts[i] = (double)part.numerator / (double)part.denominator;
    }
    double limit = latitude ? 90.0 : 180.0;
    if (parts[0] > limit || parts[1] >= 60.0 || parts[2] >= 60.0 ||
        (parts[0] == limit && (parts[1] != 0.0 || parts[2] != 0.0))) {
        return false;
    }
    *value = parts[0] + parts[1] / 60.0 + parts[2] / 3600.0;
    if (direction == 'S' || direction == 'W') {
        *value = -*value;
    }
    return true;
}

bool photoc_metadata_read_exif(ExifData *data, Photo *photo)
{
    bool ok =
        copy_ascii(entry(data, EXIF_IFD_0, EXIF_TAG_MAKE),
                   &photo->camera_make) &&
        copy_ascii(entry(data, EXIF_IFD_0, EXIF_TAG_MODEL),
                   &photo->camera_model) &&
        copy_ascii(entry(data, EXIF_IFD_EXIF, EXIF_TAG_LENS_MODEL),
                   &photo->lens_model) &&
        copy_ascii(entry(data, EXIF_IFD_EXIF, EXIF_TAG_DATE_TIME_ORIGINAL),
                   &photo->capture_timestamp);
    if (ok) {
        ExifByteOrder order = exif_data_get_byte_order(data);
        ExifEntry *orientation = entry(data, EXIF_IFD_0, EXIF_TAG_ORIENTATION);
        if (orientation != NULL && orientation->format == EXIF_FORMAT_SHORT &&
            orientation->components == 1 && orientation->size >= 2 &&
            orientation->data != NULL) {
            uint16_t value = exif_get_short(orientation->data, order);
            if (value >= 1 && value <= 8) {
                photo->orientation = value;
                photo->has_orientation = true;
            }
        }
        photo->has_iso =
            read_iso(entry(data, EXIF_IFD_EXIF, EXIF_TAG_ISO_SPEED_RATINGS),
                     order, &photo->iso);
        photo->has_aperture =
            read_rational(entry(data, EXIF_IFD_EXIF, EXIF_TAG_FNUMBER), order,
                          &photo->aperture);
        photo->has_exposure_time =
            read_rational(entry(data, EXIF_IFD_EXIF, EXIF_TAG_EXPOSURE_TIME),
                          order, &photo->exposure_time);
        photo->has_focal_length =
            read_rational(entry(data, EXIF_IFD_EXIF, EXIF_TAG_FOCAL_LENGTH),
                          order, &photo->focal_length);
        ExifEntry *equivalent =
            entry(data, EXIF_IFD_EXIF, EXIF_TAG_FOCAL_LENGTH_IN_35MM_FILM);
        if (equivalent != NULL && equivalent->format == EXIF_FORMAT_SHORT &&
            equivalent->components == 1 && equivalent->size >= 2 &&
            equivalent->data != NULL) {
            photo->focal_length_35mm = exif_get_short(equivalent->data, order);
            photo->has_focal_length_35mm = photo->focal_length_35mm != 0;
        }
        double latitude;
        double longitude;
        if (read_coordinate(
                entry(data, EXIF_IFD_GPS, EXIF_TAG_GPS_LATITUDE),
                entry(data, EXIF_IFD_GPS, EXIF_TAG_GPS_LATITUDE_REF), order,
                true, &latitude) &&
            read_coordinate(
                entry(data, EXIF_IFD_GPS, EXIF_TAG_GPS_LONGITUDE),
                entry(data, EXIF_IFD_GPS, EXIF_TAG_GPS_LONGITUDE_REF), order,
                false, &longitude)) {
            photo->latitude = latitude;
            photo->longitude = longitude;
            photo->has_gps = true;
        }
    }
    return ok;
}

enum {
    EXIF_APP1_MARKER = 0xe1,
    EXIF_HEADER_LENGTH = 6,
    EXIF_CAPTURE_LIMIT = 1024 * 1024
};

typedef struct {
    unsigned char *data;
    unsigned int length;
} exif_capture;

/* Copies the first EXIF APP1 payload (including its "Exif\0\0" header) so the
   file needs only this single inspection pass. */
static int capture_exif(unsigned char marker, const unsigned char *payload,
                        unsigned int length, bool after_scan, void *user_data)
{
    (void)after_scan;
    exif_capture *capture = user_data;
    if (capture->data != NULL || marker != EXIF_APP1_MARKER ||
        length < EXIF_HEADER_LENGTH ||
        memcmp(payload, "Exif\0\0", EXIF_HEADER_LENGTH) != 0 ||
        length > EXIF_CAPTURE_LIMIT) {
        return 0;
    }
    unsigned char *copy = malloc(length);
    if (copy == NULL) {
        return 0;
    }
    memcpy(copy, payload, length);
    capture->data = copy;
    capture->length = length;
    return 0;
}

static bool load_jpeg_exif(const unsigned char *data, unsigned int length,
                           Photo *photo)
{
    if (data == NULL || length == 0) {
        return true; /* A JPEG need not carry EXIF. */
    }
    ExifData *exif = exif_data_new_from_data(data, length);
    if (exif == NULL) {
        return true;
    }
    bool ok = photoc_metadata_read_exif(exif, photo);
    exif_data_unref(exif);
    return ok;
}

photoc_metadata_result photo_load_metadata(const char *path, Photo *photo)
{
    if (path == NULL || path[0] == '\0' || photo == NULL) {
        return PHOTOC_METADATA_INVALID_ARGUMENT;
    }
    photoc_photo_format format = photoc_format_from_path(path);
    if (format == PHOTOC_FORMAT_UNKNOWN) {
        return PHOTOC_METADATA_UNSUPPORTED_FORMAT;
    }

    int descriptor = open(path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW);
    if (descriptor < 0) {
        if (errno == ELOOP) {
            errno = EINVAL; /* Symlinks are refused, as before. */
        }
        return PHOTOC_METADATA_IO_ERROR;
    }
    struct stat file_info;
    if (fstat(descriptor, &file_info) != 0) {
        int saved_errno = errno;
        close(descriptor);
        errno = saved_errno;
        return PHOTOC_METADATA_IO_ERROR;
    }
    if (!S_ISREG(file_info.st_mode) || file_info.st_size < 0) {
        int error = S_ISDIR(file_info.st_mode) ? EISDIR
                    : file_info.st_size < 0    ? EOVERFLOW
                                               : EINVAL;
        close(descriptor);
        errno = error;
        return PHOTOC_METADATA_IO_ERROR;
    }
    uint64_t size = (uint64_t)file_info.st_size;
    FILE *file = fdopen(descriptor, "rb");
    if (file == NULL) {
        int saved_errno = errno;
        close(descriptor);
        errno = saved_errno;
        return PHOTOC_METADATA_IO_ERROR;
    }

    Photo loaded = {0};
    exif_capture capture = {0};
    photoc_metadata_result result = PHOTOC_METADATA_OK;
    if (photo_init(&loaded, path) != 0) {
        result = PHOTOC_METADATA_NO_MEMORY;
    } else {
        loaded.format = format;
        loaded.file_size = size;
        loaded.has_file_size = true;
        photoc_jpeg_info info;
        if (format == PHOTOC_FORMAT_SONY_ARW) {
            result = photoc_arw_load_metadata(file, size, &loaded);
        } else if (photoc_jpeg_inspect_app(file, &info, capture_exif,
                                           &capture) != 0) {
            result = errno == EINVAL ? PHOTOC_METADATA_INVALID_JPEG
                                     : PHOTOC_METADATA_IO_ERROR;
        } else {
            loaded.width = info.width;
            loaded.height = info.height;
            loaded.has_width = true;
            loaded.has_height = true;
        }
    }
    int saved_errno = errno;
    if (fclose(file) != 0 && result == PHOTOC_METADATA_OK) {
        result = PHOTOC_METADATA_IO_ERROR;
        saved_errno = errno;
    }
    if (result == PHOTOC_METADATA_OK && format == PHOTOC_FORMAT_JPEG &&
        !load_jpeg_exif(capture.data, capture.length, &loaded)) {
        result = PHOTOC_METADATA_NO_MEMORY;
    }
    free(capture.data);
    if (result == PHOTOC_METADATA_OK) {
        *photo = loaded;
    } else {
        photo_cleanup(&loaded);
        errno = saved_errno;
    }
    return result;
}

const char *photo_metadata_result_message(photoc_metadata_result result)
{
    switch (result) {
    case PHOTOC_METADATA_OK:
        return "metadata loaded";
    case PHOTOC_METADATA_INVALID_ARGUMENT:
        return "invalid metadata loader argument";
    case PHOTOC_METADATA_UNSUPPORTED_FORMAT:
        return "unsupported file type (expected .jpg, .jpeg, or Sony .arw)";
    case PHOTOC_METADATA_INVALID_JPEG:
        return "invalid or truncated JPEG file";
    case PHOTOC_METADATA_IO_ERROR:
        return "unable to read photo file";
    case PHOTOC_METADATA_INVALID_ARW:
        return "invalid or truncated ARW metadata";
    case PHOTOC_METADATA_RESOURCE_LIMIT:
        return "ARW metadata exceeds supported resource limits";
    case PHOTOC_METADATA_PRIVACY_REMAINS:
        return "opaque MakerNote or unsupported metadata may retain private "
               "information";
    case PHOTOC_METADATA_NO_MEMORY:
        return "out of memory while loading metadata";
    default:
        return "unknown metadata error";
    }
}
