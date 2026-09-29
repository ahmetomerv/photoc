#include "photoc/format.h"

#include "photoc/fs.h"

photoc_photo_format photoc_format_from_path(const char *path)
{
    if (photoc_fs_is_jpeg(path))
        return PHOTOC_FORMAT_JPEG;
    if (photoc_fs_is_arw(path))
        return PHOTOC_FORMAT_SONY_ARW;
    return PHOTOC_FORMAT_UNKNOWN;
}

const char *photoc_format_name(photoc_photo_format format)
{
    switch (format) {
    case PHOTOC_FORMAT_JPEG:
        return "jpeg";
    case PHOTOC_FORMAT_SONY_ARW:
        return "sony_arw";
    case PHOTOC_FORMAT_UNKNOWN:
        return "unknown";
    }
    return "unknown";
}

bool photoc_format_is_selected(const char *path, unsigned int formats)
{
    photoc_photo_format format = photoc_format_from_path(path);
    return format != PHOTOC_FORMAT_UNKNOWN && (formats & (1u << format)) != 0;
}
