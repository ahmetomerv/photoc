#include "photoc/scan.h"

#include "photoc/fs.h"

#include <errno.h>
#include <stddef.h>

typedef struct {
    photoc_scan_photo_fn on_photo;
    photoc_scan_warning_fn on_warning;
    void *user_data;
    photoc_scan_stats *stats;
    bool out_of_memory;
} scan_context;

static bool scan_entry(const char *path, photoc_fs_type type, void *user_data)
{
    scan_context *context = user_data;
    if (type != PHOTOC_FS_FILE) {
        return true;
    }

    ++context->stats->files_visited;
    if (!photoc_fs_is_jpeg(path)) {
        ++context->stats->skipped_files;
        return true;
    }
    ++context->stats->jpeg_files_found;

    Photo photo = {0};
    photoc_metadata_result result = photo_load_metadata(path, &photo);
    if (result != PHOTOC_METADATA_OK) {
        int system_errno = result == PHOTOC_METADATA_IO_ERROR ? errno : 0;
        ++context->stats->errors;
        ++context->stats->skipped_files;
        if (context->on_warning != NULL) {
            context->on_warning(path, result, system_errno,
                                context->user_data);
        }
        if (result == PHOTOC_METADATA_NO_MEMORY) {
            context->out_of_memory = true;
            return false;
        }
        return true;
    }

    ++context->stats->photos_parsed;
    bool keep_scanning = context->on_photo(&photo, context->user_data);
    photo_cleanup(&photo);
    return keep_scanning;
}

int photoc_scan_directory(const char *directory, bool recursive,
                          photoc_scan_photo_fn on_photo,
                          photoc_scan_warning_fn on_warning,
                          void *user_data, photoc_scan_stats *stats)
{
    if (directory == NULL || directory[0] == '\0' || on_photo == NULL ||
        stats == NULL) {
        errno = EINVAL;
        return -1;
    }
    *stats = (photoc_scan_stats){0};

    scan_context context = {
        .on_photo = on_photo,
        .on_warning = on_warning,
        .user_data = user_data,
        .stats = stats,
        .out_of_memory = false
    };
    int result = recursive ?
        photoc_fs_walk_recursive(directory, scan_entry, &context) :
        photoc_fs_walk(directory, scan_entry, &context);
    if (context.out_of_memory) {
        errno = ENOMEM;
        return -1;
    }
    return result;
}
