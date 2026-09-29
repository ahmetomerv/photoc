#include "photoc/photo.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int photo_init(Photo *photo, const char *path)
{
    if (photo == NULL || path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    size_t length = strlen(path);
    if (length == SIZE_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    char *owned_path = malloc(length + 1);
    if (owned_path == NULL) {
        return -1;
    }
    memcpy(owned_path, path, length + 1);

    *photo = (Photo){0};
    photo->path = owned_path;
    return 0;
}

void photo_cleanup(Photo *photo)
{
    if (photo == NULL) {
        return;
    }
    free(photo->path);
    free(photo->camera_make);
    free(photo->camera_model);
    free(photo->lens_model);
    free(photo->capture_timestamp);
    *photo = (Photo){0};
}
