#define _POSIX_C_SOURCE 200809L

#include "review_model.h"

#include "photoc/fs.h"
#include "photoc/photo.h"
#include "photoc/timestamp.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    review_model model;
    size_t capacity;
    const char *root; /* Borrowed during the walk. */
    int error;
} review_collection;

static bool valid_sort(review_sort sort)
{
    return sort == PHOTOC_REVIEW_SORT_NAME || sort == PHOTOC_REVIEW_SORT_DATE;
}

static bool valid_show(review_show show)
{
    return show >= PHOTOC_REVIEW_SHOW_ALL &&
           show <= PHOTOC_REVIEW_SHOW_REJECTED;
}

static bool valid_status(photoc_review_status status)
{
    return status >= PHOTOC_REVIEW_UNMARKED &&
           status <= PHOTOC_REVIEW_REJECTED;
}

static bool collect_file(const char *path, photoc_fs_type type, void *user_data)
{
    review_collection *collection = user_data;
    if (type != PHOTOC_FS_FILE || !photoc_fs_is_jpeg(path)) {
        return true;
    }
    if (collection->model.count == collection->capacity) {
        size_t next = collection->capacity == 0 ? 32 : collection->capacity * 2;
        if (next < collection->capacity ||
            next > SIZE_MAX / sizeof(*collection->model.items)) {
            collection->error = EOVERFLOW;
            return false;
        }
        review_item *grown =
            realloc(collection->model.items, next * sizeof(*grown));
        if (grown == NULL) {
            collection->error = ENOMEM;
            return false;
        }
        collection->model.items = grown;
        collection->capacity = next;
    }

    const char *relative = photoc_fs_relative(collection->root, path);
    char *path_copy = strdup(path);
    char *relative_copy = strdup(relative);
    if (path_copy == NULL || relative_copy == NULL) {
        free(path_copy);
        free(relative_copy);
        collection->error = ENOMEM;
        return false;
    }
    collection->model.items[collection->model.count++] =
        (review_item){.path = path_copy, .relative_path = relative_copy};
    return true;
}

static int compare_name(const void *left, const void *right)
{
    const review_item *a = left;
    const review_item *b = right;
    return photoc_fs_compare_name_then_path(a->relative_path,
                                            b->relative_path);
}

static int compare_date(const void *left, const void *right)
{
    const review_item *a = left;
    const review_item *b = right;
    if (a->capture_timestamp == NULL && b->capture_timestamp != NULL)
        return 1;
    if (a->capture_timestamp != NULL && b->capture_timestamp == NULL)
        return -1;
    if (a->capture_timestamp != NULL) {
        int order = strcmp(a->capture_timestamp, b->capture_timestamp);
        if (order != 0)
            return order;
    }
    return strcmp(a->relative_path, b->relative_path);
}

static int load_dates(review_model *model)
{
    for (size_t i = 0; i < model->count; ++i) {
        Photo photo = {0};
        photoc_metadata_result result =
            photo_load_metadata(model->items[i].path, &photo);
        if (result == PHOTOC_METADATA_NO_MEMORY) {
            errno = ENOMEM;
            return -1;
        }
        if (result == PHOTOC_METADATA_OK &&
            photoc_timestamp_is_valid(photo.capture_timestamp)) {
            model->items[i].capture_timestamp =
                strdup(photo.capture_timestamp);
            if (model->items[i].capture_timestamp == NULL) {
                photo_cleanup(&photo);
                return -1;
            }
        }
        photo_cleanup(&photo);
    }
    return 0;
}

static bool matches(review_show show, photoc_review_status status)
{
    return show == PHOTOC_REVIEW_SHOW_ALL ||
           (show == PHOTOC_REVIEW_SHOW_UNMARKED &&
            status == PHOTOC_REVIEW_UNMARKED) ||
           (show == PHOTOC_REVIEW_SHOW_PICKED &&
            status == PHOTOC_REVIEW_PICKED) ||
           (show == PHOTOC_REVIEW_SHOW_REJECTED &&
            status == PHOTOC_REVIEW_REJECTED);
}

int review_model_rebuild(review_model *model)
{
    if (model == NULL || !valid_show(model->show) ||
        (model->count != 0 && (model->items == NULL || model->visible == NULL))) {
        errno = EINVAL;
        return -1;
    }
    for (size_t i = 0; i < model->count; ++i) {
        if (!valid_status(model->items[i].status)) {
            errno = EINVAL;
            return -1;
        }
    }

    model->picked = 0;
    model->rejected = 0;
    model->unmarked = 0;
    model->visible_count = 0;
    model->cursor = 0;
    for (size_t i = 0; i < model->count; ++i) {
        photoc_review_status status = model->items[i].status;
        if (status == PHOTOC_REVIEW_PICKED)
            ++model->picked;
        else if (status == PHOTOC_REVIEW_REJECTED)
            ++model->rejected;
        else
            ++model->unmarked;
        if (matches(model->show, status))
            model->visible[model->visible_count++] = i;
    }
    return 0;
}

int review_model_load(review_model *out, const char *root, bool recursive,
                      review_sort sort, review_show show)
{
    if (out == NULL || root == NULL || root[0] == '\0' ||
        !valid_sort(sort) || !valid_show(show)) {
        errno = EINVAL;
        return -1;
    }
    review_collection collection = {.root = root};
    collection.model.show = show;
    int walk_result = recursive
                          ? photoc_fs_walk_recursive(root, collect_file,
                                                     &collection)
                          : photoc_fs_walk(root, collect_file, &collection);
    if (walk_result != 0) {
        int saved_errno = walk_result == 1 ? collection.error : errno;
        review_model_cleanup(&collection.model);
        errno = saved_errno != 0 ? saved_errno : EIO;
        return -1;
    }
    if (sort == PHOTOC_REVIEW_SORT_DATE && load_dates(&collection.model) != 0) {
        int saved_errno = errno;
        review_model_cleanup(&collection.model);
        errno = saved_errno;
        return -1;
    }
    if (collection.model.count != 0) {
        qsort(collection.model.items, collection.model.count,
              sizeof(*collection.model.items),
              sort == PHOTOC_REVIEW_SORT_DATE ? compare_date : compare_name);
        if (collection.model.count > SIZE_MAX / sizeof(*collection.model.visible)) {
            review_model_cleanup(&collection.model);
            errno = EOVERFLOW;
            return -1;
        }
        collection.model.visible =
            malloc(collection.model.count * sizeof(*collection.model.visible));
        if (collection.model.visible == NULL) {
            review_model_cleanup(&collection.model);
            return -1;
        }
    }
    if (review_model_rebuild(&collection.model) != 0) {
        int saved_errno = errno;
        review_model_cleanup(&collection.model);
        errno = saved_errno;
        return -1;
    }
    *out = collection.model;
    return 0;
}

const review_item *review_model_current(const review_model *model)
{
    if (model == NULL || model->cursor >= model->visible_count)
        return NULL;
    return &model->items[model->visible[model->cursor]];
}

bool review_model_next(review_model *model)
{
    if (model == NULL || model->cursor + 1 >= model->visible_count)
        return false;
    ++model->cursor;
    return true;
}

bool review_model_previous(review_model *model)
{
    if (model == NULL || model->visible_count == 0 || model->cursor == 0)
        return false;
    --model->cursor;
    return true;
}

int review_model_mark_current(review_model *model, photoc_review_status status)
{
    if (model == NULL || !valid_status(status) ||
        model->cursor >= model->visible_count) {
        errno = EINVAL;
        return -1;
    }
    review_item *item = &model->items[model->visible[model->cursor]];
    if (item->status != status) {
        if (item->status == PHOTOC_REVIEW_PICKED)
            --model->picked;
        else if (item->status == PHOTOC_REVIEW_REJECTED)
            --model->rejected;
        else
            --model->unmarked;
        item->status = status;
        if (status == PHOTOC_REVIEW_PICKED)
            ++model->picked;
        else if (status == PHOTOC_REVIEW_REJECTED)
            ++model->rejected;
        else
            ++model->unmarked;
    }

    if (!matches(model->show, item->status)) {
        size_t remaining = model->visible_count - model->cursor - 1;
        memmove(&model->visible[model->cursor],
                &model->visible[model->cursor + 1],
                remaining * sizeof(*model->visible));
        --model->visible_count;
        if (model->visible_count == 0)
            model->cursor = 0;
        else if (model->cursor == model->visible_count)
            --model->cursor;
    } else if (model->cursor + 1 < model->visible_count) {
        ++model->cursor;
    }
    return 0;
}

void review_model_cleanup(review_model *model)
{
    if (model == NULL)
        return;
    for (size_t i = 0; i < model->count; ++i) {
        free(model->items[i].path);
        free(model->items[i].relative_path);
        free(model->items[i].capture_timestamp);
    }
    free(model->items);
    free(model->visible);
    *model = (review_model){0};
}
