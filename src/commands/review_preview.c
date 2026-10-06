#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE

#include "review_preview.h"

#include "photoc/jpeg_metadata.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

#define PREVIEW_MIN_SOURCE_BYTES (512u * 1024u)
#define PREVIEW_MAX_SOURCE_BYTES (64u * 1024u * 1024u)
#define PREVIEW_MAX_DIMENSION 1600u
#define PREVIEW_QUALITY 82
#define PREVIEW_CACHE_BYTES (8u * 1024u * 1024u)

bool review_source_equal(const struct stat *left, const struct stat *right)
{
#if defined(__APPLE__)
    const struct timespec *left_modified = &left->st_mtimespec;
    const struct timespec *right_modified = &right->st_mtimespec;
    const struct timespec *left_changed = &left->st_ctimespec;
    const struct timespec *right_changed = &right->st_ctimespec;
#else
    const struct timespec *left_modified = &left->st_mtim;
    const struct timespec *right_modified = &right->st_mtim;
    const struct timespec *left_changed = &left->st_ctim;
    const struct timespec *right_changed = &right->st_ctim;
#endif
    return left->st_dev == right->st_dev && left->st_ino == right->st_ino &&
           left->st_size == right->st_size &&
           left_modified->tv_sec == right_modified->tv_sec &&
           left_modified->tv_nsec == right_modified->tv_nsec &&
           left_changed->tv_sec == right_changed->tv_sec &&
           left_changed->tv_nsec == right_changed->tv_nsec;
}

static void evict(review_preview_cache *cache, review_preview_entry *entry)
{
    if (entry == NULL)
        return;
    cache->bytes -= entry->jpeg.size;
    photoc_jpeg_buffer_cleanup(&entry->jpeg);
    *entry = (review_preview_entry){0};
}

static review_preview_entry *oldest_entry(review_preview_cache *cache)
{
    review_preview_entry *oldest = NULL;
    for (size_t i = 0; i < REVIEW_PREVIEW_SLOTS; ++i) {
        review_preview_entry *entry = &cache->entries[i];
        if (entry->occupied &&
            (oldest == NULL || entry->used_at < oldest->used_at))
            oldest = entry;
    }
    return oldest;
}

static const photoc_jpeg_buffer *remember(review_preview_cache *cache,
                                          size_t item_index,
                                          const struct stat *source,
                                          photoc_jpeg_buffer preview)
{
    while (cache->bytes + preview.size > PREVIEW_CACHE_BYTES) {
        review_preview_entry *stale = oldest_entry(cache);
        if (stale == NULL) {
            cache->bytes = 0;
            break;
        }
        evict(cache, stale);
    }
    review_preview_entry *slot = NULL;
    for (size_t i = 0; i < REVIEW_PREVIEW_SLOTS; ++i) {
        if (!cache->entries[i].occupied) {
            slot = &cache->entries[i];
            break;
        }
    }
    if (slot == NULL) {
        slot = oldest_entry(cache);
        if (slot == NULL) {
            photoc_jpeg_buffer_cleanup(&preview);
            return NULL;
        }
        evict(cache, slot);
    }
    *slot = (review_preview_entry){.item_index = item_index,
                                   .source = *source,
                                   .jpeg = preview,
                                   .used_at = ++cache->clock,
                                   .occupied = true};
    cache->bytes += preview.size;
    return preview.data == NULL ? NULL : &slot->jpeg;
}

const photoc_jpeg_buffer *review_preview_get(review_preview_cache *cache,
                                             size_t item_index,
                                             const char *path,
                                             const Photo *metadata)
{
    if (cache == NULL || path == NULL || metadata == NULL)
        return NULL;
    struct stat source;
    if (lstat(path, &source) != 0 || !S_ISREG(source.st_mode))
        return NULL;
    for (size_t i = 0; i < REVIEW_PREVIEW_SLOTS; ++i) {
        review_preview_entry *entry = &cache->entries[i];
        if (!entry->occupied || entry->item_index != item_index)
            continue;
        if (review_source_equal(&entry->source, &source)) {
            entry->used_at = ++cache->clock;
            return entry->jpeg.data == NULL ? NULL : &entry->jpeg;
        }
        evict(cache, entry);
        break;
    }
    if (source.st_size < PREVIEW_MIN_SOURCE_BYTES ||
        (uintmax_t)source.st_size > PREVIEW_MAX_SOURCE_BYTES)
        return NULL;

    /* The encoder drops ICC. Keep the original for photos whose profile
       could change the displayed colors if omitted from a preview. */
    photoc_jpeg_metadata *jpeg_metadata = NULL;
    if (photoc_jpeg_metadata_load_copy(path, &jpeg_metadata) !=
        PHOTOC_JPEG_EDIT_OK)
        return NULL;
    bool has_icc = photoc_jpeg_metadata_has_icc(jpeg_metadata);
    photoc_jpeg_metadata_free(jpeg_metadata);
    if (has_icc)
        return remember(cache, item_index, &source, (photoc_jpeg_buffer){0});

    photoc_image decoded = {0};
    photoc_jpeg_buffer preview = {0};
    photoc_image_result result = photoc_image_decode_jpeg_scaled_bounded(
        path, PREVIEW_MAX_DIMENSION, PREVIEW_MAX_SOURCE_BYTES, &decoded);
    if (result == PHOTOC_IMAGE_OK && metadata->has_orientation)
        result =
            photoc_image_apply_orientation(&decoded, metadata->orientation);
    if (result == PHOTOC_IMAGE_OK)
        result = photoc_image_encode_jpeg(&decoded, PREVIEW_QUALITY, &preview);
    photoc_image_cleanup(&decoded);
    if (result != PHOTOC_IMAGE_OK)
        return remember(cache, item_index, &source, (photoc_jpeg_buffer){0});

    struct stat current;
    if (lstat(path, &current) != 0 || !review_source_equal(&source, &current) ||
        preview.size > PREVIEW_CACHE_BYTES) {
        photoc_jpeg_buffer_cleanup(&preview);
        return NULL;
    }
    if ((uintmax_t)preview.size * 5 > (uintmax_t)source.st_size * 4) {
        photoc_jpeg_buffer_cleanup(&preview);
        return remember(cache, item_index, &source, (photoc_jpeg_buffer){0});
    }
    return remember(cache, item_index, &source, preview);
}

void review_preview_cleanup(review_preview_cache *cache)
{
    if (cache == NULL)
        return;
    for (size_t i = 0; i < REVIEW_PREVIEW_SLOTS; ++i)
        photoc_jpeg_buffer_cleanup(&cache->entries[i].jpeg);
    *cache = (review_preview_cache){0};
}
