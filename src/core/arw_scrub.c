#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/arw.h"

#include "jpeg.h"
#include "metadata_internal.h"
#include "tiff.h"
#include "xmp_internal.h"
#include "photoc/fs.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define SCRUB_MAX_XMP_BYTES (UINT64_C(4) * 1024 * 1024)
#define SCRUB_MAX_SEGMENTS 65536u
#define SCRUB_CHUNK 65536u

typedef struct {
    uint64_t position; /* The 12-byte directory entry. */
    uint64_t value;    /* Value bytes; inline values live inside the entry. */
    uint64_t size;
    uint32_t directory;
    uint32_t count;
    uint16_t tag;
    uint16_t type;
    photoc_tiff_directory_kind kind;
    bool removed;
    bool rewritten; /* Value bytes replaced by a patch; do not keep. */
} scan_entry;

typedef struct {
    scan_entry *entries; /* Owned. */
    size_t count;
    size_t capacity;
} entry_list;

/* bytes == NULL writes zeros. */
typedef struct {
    uint64_t offset;
    uint64_t length;
    unsigned char *bytes; /* Owned. */
} patch;

typedef struct {
    uint64_t start;
    uint64_t end;
} byte_range;

typedef struct {
    patch *patches; /* Owned, with every patch's bytes. */
    size_t patch_count;
    size_t patch_capacity;
    byte_range *kept; /* Owned. Ranges referenced by data that is kept. */
    size_t kept_count;
    size_t kept_capacity;
} edit_plan;

typedef struct {
    FILE *file; /* Borrowed source stream. */
    uint64_t size;
    ExifByteOrder order;
    entry_list list;
    edit_plan plan;
} scrub_context;

static bool grow(void **items, size_t *capacity, size_t count, size_t unit)
{
    if (count < *capacity)
        return true;
    size_t next = *capacity == 0 ? 64 : *capacity * 2;
    if (next < *capacity || next > SIZE_MAX / unit) {
        errno = ENOMEM;
        return false;
    }
    void *grown = realloc(*items, next * unit);
    if (grown == NULL)
        return false;
    *items = grown;
    *capacity = next;
    return true;
}

static bool collect_entry(photoc_tiff_reader *reader,
                          const photoc_tiff_entry *entry, void *data)
{
    (void)reader;
    entry_list *list = data;
    void *items = list->entries;
    if (!grow(&items, &list->capacity, list->count, sizeof(scan_entry)))
        return false;
    list->entries = items;
    list->entries[list->count++] =
        (scan_entry){.position = entry->position,
                     .value = entry->offset,
                     .size = entry->size,
                     .directory = entry->directory_offset,
                     .count = entry->count,
                     .tag = entry->tag,
                     .type = entry->type,
                     .kind = entry->kind};
    return true;
}

static bool add_patch(edit_plan *plan, uint64_t offset, uint64_t length,
                      unsigned char *bytes)
{
    void *items = plan->patches;
    if (!grow(&items, &plan->patch_capacity, plan->patch_count,
              sizeof(patch))) {
        free(bytes);
        return false;
    }
    plan->patches = items;
    plan->patches[plan->patch_count++] = (patch){offset, length, bytes};
    return true;
}

static bool keep(edit_plan *plan, uint64_t start, uint64_t length)
{
    if (length == 0)
        return true;
    void *items = plan->kept;
    if (!grow(&items, &plan->kept_capacity, plan->kept_count,
              sizeof(byte_range)))
        return false;
    plan->kept = items;
    plan->kept[plan->kept_count++] = (byte_range){start, start + length};
    return true;
}

static void free_plan(edit_plan *plan)
{
    for (size_t i = 0; i < plan->patch_count; ++i)
        free(plan->patches[i].bytes);
    free(plan->patches);
    free(plan->kept);
    *plan = (edit_plan){0};
}

static bool read_at(FILE *file, uint64_t offset, unsigned char *bytes,
                    size_t length)
{
    errno = 0;
    if (fseeko(file, (off_t)offset, SEEK_SET) != 0 ||
        fread(bytes, 1, length, file) != length) {
        if (errno == 0)
            errno = EIO;
        return false;
    }
    return true;
}

static bool is_identifier(const scan_entry *entry)
{
    if (entry->kind == PHOTOC_TIFF_IMAGE)
        return entry->tag == EXIF_TAG_ARTIST ||
               entry->tag == EXIF_TAG_IMAGE_UNIQUE_ID;
    if (entry->kind == PHOTOC_TIFF_EXIF)
        return entry->tag == EXIF_TAG_CAMERA_OWNER_NAME ||
               entry->tag == EXIF_TAG_BODY_SERIAL_NUMBER ||
               entry->tag == EXIF_TAG_LENS_SERIAL_NUMBER ||
               entry->tag == EXIF_TAG_IMAGE_UNIQUE_ID;
    return false;
}

static size_t directory_entries(const entry_list *list, uint32_t directory,
                                size_t *first)
{
    size_t count = 0;
    for (size_t i = 0; i < list->count; ++i) {
        if (list->entries[i].directory != directory)
            continue;
        if (count == 0)
            *first = i;
        ++count;
    }
    return count;
}

static uint64_t table_length(size_t entries)
{
    return 2 + (uint64_t)entries * 12 + 4;
}

/* Rewrite a directory without its removed entries: retained entries keep
   their order, the next-directory pointer moves up, and freed bytes become
   zeros, so the table occupies exactly its original bytes. */
static photoc_jpeg_edit_result rebuild_directory(scrub_context *context,
                                                 uint32_t directory)
{
    size_t first = 0;
    size_t entries = directory_entries(&context->list, directory, &first);
    uint64_t length = table_length(entries);
    unsigned char *original = malloc((size_t)length);
    unsigned char *rebuilt = calloc(1, (size_t)length);
    if (original == NULL || rebuilt == NULL) {
        free(original);
        free(rebuilt);
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    }
    if (!read_at(context->file, directory, original, (size_t)length)) {
        free(original);
        free(rebuilt);
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    size_t kept = 0;
    for (size_t i = first; i < first + entries; ++i) {
        const scan_entry *entry = &context->list.entries[i];
        if (entry->removed)
            continue;
        memcpy(rebuilt + 2 + kept * 12,
               original + (entry->position - directory), 12);
        ++kept;
    }
    exif_set_short(rebuilt, context->order, (ExifShort)kept);
    memcpy(rebuilt + 2 + kept * 12, original + 2 + entries * 12, 4);
    free(original);
    return add_patch(&context->plan, directory, length, rebuilt)
               ? PHOTOC_JPEG_EDIT_OK
               : PHOTOC_JPEG_EDIT_NO_MEMORY;
}

static const char *find_text(const unsigned char *bytes, size_t length,
                             const char *text)
{
    size_t text_length = strlen(text);
    for (size_t i = 0; i + text_length <= length; ++i) {
        if (memcmp(bytes + i, text, text_length) == 0)
            return (const char *)bytes + i;
    }
    return NULL;
}

/* Remove private XMP properties, then pad with spaces to the original packet
   size, before the xpacket trailer when present. */
static photoc_jpeg_edit_result scrub_xmp_packet(scrub_context *context,
                                                scan_entry *entry,
                                                photoc_scrub_report *report)
{
    if (entry->size > SCRUB_MAX_XMP_BYTES)
        return PHOTOC_JPEG_EDIT_UNSUPPORTED_XMP;
    unsigned char *packet = malloc((size_t)entry->size);
    if (packet == NULL)
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    if (!read_at(context->file, entry->value, packet, (size_t)entry->size)) {
        free(packet);
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    size_t length = (size_t)entry->size;
    while (length > 0 && packet[length - 1] == 0)
        --length;
    unsigned char *xml = NULL;
    size_t xml_length = 0;
    bool changed = false;
    photoc_jpeg_edit_result result = photoc_xmp_remove_private(
        (const char *)packet, length, &xml, &xml_length, &changed);
    free(packet);
    if (result != PHOTOC_JPEG_EDIT_OK || !changed)
        return result;
    if (xml_length > entry->size) {
        free(xml);
        return PHOTOC_JPEG_EDIT_UNSUPPORTED_XMP;
    }
    unsigned char *padded = malloc((size_t)entry->size);
    if (padded == NULL) {
        free(xml);
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    }
    const char *trailer = find_text(xml, xml_length, "<?xpacket end");
    size_t head =
        trailer == NULL ? xml_length : (size_t)(trailer - (const char *)xml);
    size_t padding = (size_t)entry->size - xml_length;
    memcpy(padded, xml, head);
    memset(padded + head, ' ', padding);
    memcpy(padded + head + padding, xml + head, xml_length - head);
    free(xml);
    report->xmp_fields = true;
    entry->rewritten = true;
    return add_patch(&context->plan, entry->value, entry->size, padded)
               ? PHOTOC_JPEG_EDIT_OK
               : PHOTOC_JPEG_EDIT_NO_MEMORY;
}

static bool entry_scalar(scrub_context *context, const scan_entry *entry,
                         uint32_t index, uint32_t *value)
{
    unsigned int unit = entry->type == 3 ? 2u : 4u;
    unsigned char bytes[4];
    if ((entry->type != 3 && entry->type != 4) || index >= entry->count ||
        !read_at(context->file, entry->value + (uint64_t)index * unit, bytes,
                 unit))
        return false;
    *value = unit == 2 ? exif_get_short(bytes, context->order)
                       : exif_get_long(bytes, context->order);
    return true;
}

static const scan_entry *find_entry(const entry_list *list, uint32_t directory,
                                    uint16_t tag)
{
    for (size_t i = 0; i < list->count; ++i) {
        if (list->entries[i].directory == directory &&
            list->entries[i].kind == PHOTOC_TIFF_IMAGE &&
            list->entries[i].tag == tag)
            return &list->entries[i];
    }
    return NULL;
}

static int preview_meta_visitor(unsigned char marker,
                                const unsigned char *payload,
                                unsigned int length, bool after_scan,
                                void *user_data)
{
    (void)payload;
    (void)length;
    (void)after_scan;
    if (marker == 0xe1 || marker == 0xed || marker == 0xfe)
        ((photoc_scrub_report *)user_data)->unhandled_metadata = true;
    return 0;
}

/* Keep preview JPEGs and sensor strips/tiles referenced by offset arrays, and
   note metadata inside embedded JPEGs, which is never edited. */
static photoc_jpeg_edit_result keep_image_data(scrub_context *context,
                                               uint32_t directory,
                                               photoc_scrub_report *report)
{
    static const uint16_t pairs[3][2] = {
        {0x0201, 0x0202}, {0x0111, 0x0117}, {0x0144, 0x0145}};
    for (size_t p = 0; p < 3; ++p) {
        const scan_entry *offsets =
            find_entry(&context->list, directory, pairs[p][0]);
        const scan_entry *counts =
            find_entry(&context->list, directory, pairs[p][1]);
        if (offsets == NULL || counts == NULL)
            continue;
        if (offsets->count != counts->count ||
            offsets->count > SCRUB_MAX_SEGMENTS)
            return PHOTOC_JPEG_EDIT_UNSAFE_ARW_LAYOUT;
        for (uint32_t i = 0; i < offsets->count; ++i) {
            uint32_t offset = 0;
            uint32_t length = 0;
            if (!entry_scalar(context, offsets, i, &offset) ||
                !entry_scalar(context, counts, i, &length))
                return errno == EIO || errno == 0 ? PHOTOC_JPEG_EDIT_INVALID_ARW
                                                  : PHOTOC_JPEG_EDIT_IO_ERROR;
            if ((uint64_t)offset + length > context->size)
                return PHOTOC_JPEG_EDIT_INVALID_ARW;
            if (!keep(&context->plan, offset, length))
                return PHOTOC_JPEG_EDIT_NO_MEMORY;
            if (p != 0)
                continue;
            /* Walk the whole embedded JPEG (including post-SOS markers) with
               the shared inspector so entropy stuffing is handled. APP1,
               APP13, and COM are retained unedited. */
            if (fseeko(context->file, (off_t)offset, SEEK_SET) != 0) {
                report->unhandled_metadata = true;
                continue;
            }
            photoc_jpeg_info info;
            if (photoc_jpeg_inspect_app_limited(
                    context->file, &info, preview_meta_visitor, report,
                    (uint64_t)offset + length) != 0)
                report->unhandled_metadata = true;
        }
    }
    return PHOTOC_JPEG_EDIT_OK;
}

static photoc_jpeg_edit_result plan_edits(scrub_context *context, bool privacy,
                                          photoc_scrub_report *report)
{
    entry_list *list = &context->list;
    for (size_t i = 0; i < list->count; ++i) {
        scan_entry *entry = &list->entries[i];
        if (entry->kind == PHOTOC_TIFF_IMAGE &&
            entry->tag == EXIF_TAG_GPS_INFO_IFD_POINTER) {
            entry->removed = true;
            report->exif_gps = true;
        } else if (privacy && is_identifier(entry)) {
            entry->removed = true;
            report->exif_identifiers = true;
        }
        if ((entry->kind == PHOTOC_TIFF_EXIF &&
             entry->tag == EXIF_TAG_MAKER_NOTE) ||
            (entry->kind == PHOTOC_TIFF_IMAGE && entry->tag == 0xc634))
            report->maker_note = true;
    }

    photoc_jpeg_edit_result result = PHOTOC_JPEG_EDIT_OK;
    uint32_t previous = 0;
    bool any = false;
    for (size_t i = 0; i < list->count && result == PHOTOC_JPEG_EDIT_OK; ++i) {
        const scan_entry *entry = &list->entries[i];
        if (any && entry->directory == previous)
            continue;
        any = true;
        previous = entry->directory;
        size_t first = 0;
        size_t entries = directory_entries(list, entry->directory, &first);
        bool gps = entry->kind == PHOTOC_TIFF_GPS;
        bool edited = false;
        for (size_t j = first; j < first + entries; ++j)
            edited |= list->entries[j].removed;
        if (gps) {
            if (!add_patch(&context->plan, entry->directory,
                           table_length(entries), NULL))
                result = PHOTOC_JPEG_EDIT_NO_MEMORY;
            continue;
        }
        if (!keep(&context->plan, entry->directory, table_length(entries)))
            result = PHOTOC_JPEG_EDIT_NO_MEMORY;
        else if (edited)
            result = rebuild_directory(context, entry->directory);
        if (result == PHOTOC_JPEG_EDIT_OK && entry->kind == PHOTOC_TIFF_IMAGE)
            result = keep_image_data(context, entry->directory, report);
    }

    for (size_t i = 0; i < list->count && result == PHOTOC_JPEG_EDIT_OK; ++i) {
        scan_entry *entry = &list->entries[i];
        if (privacy && entry->kind == PHOTOC_TIFF_IMAGE &&
            entry->tag == 0x02bc && (entry->type == 1 || entry->type == 7) &&
            entry->size != 0)
            result = scrub_xmp_packet(context, entry, report);
    }

    for (size_t i = 0; i < list->count && result == PHOTOC_JPEG_EDIT_OK; ++i) {
        const scan_entry *entry = &list->entries[i];
        if (entry->size <= 4)
            continue; /* Inline values disappear with their entry. */
        bool erase = entry->removed || entry->kind == PHOTOC_TIFF_GPS;
        if (erase) {
            if (!add_patch(&context->plan, entry->value, entry->size, NULL))
                result = PHOTOC_JPEG_EDIT_NO_MEMORY;
        } else if (!entry->rewritten &&
                   !keep(&context->plan, entry->value, entry->size)) {
            result = PHOTOC_JPEG_EDIT_NO_MEMORY;
        }
    }
    return result;
}

static int compare_patches(const void *left, const void *right)
{
    const patch *a = left;
    const patch *b = right;
    return a->offset < b->offset ? -1 : a->offset > b->offset ? 1 : 0;
}

/* Patches must not overlap each other, and must not touch bytes a kept entry,
   directory, preview, or sensor strip still references. A replacement may
   rewrite exactly one kept span (the IFD table being rebuilt); any other
   overlap with retained data is unsafe. */
static bool plan_is_safe(edit_plan *plan)
{
    if (plan->patch_count > 1)
        qsort(plan->patches, plan->patch_count, sizeof(*plan->patches),
              compare_patches);
    for (size_t i = 0; i < plan->patch_count; ++i) {
        const patch *current = &plan->patches[i];
        uint64_t end = current->offset + current->length;
        if (i + 1 < plan->patch_count && plan->patches[i + 1].offset < end)
            return false;
        for (size_t k = 0; k < plan->kept_count; ++k) {
            if (current->offset >= plan->kept[k].end ||
                plan->kept[k].start >= end)
                continue;
            if (current->bytes != NULL &&
                plan->kept[k].start == current->offset &&
                plan->kept[k].end == end)
                continue; /* IFD rebuild of this exact directory span. */
            return false;
        }
    }
    return true;
}

static void apply_patches(const edit_plan *plan, uint64_t position,
                          unsigned char *bytes, size_t length)
{
    uint64_t end = position + length;
    for (size_t i = 0; i < plan->patch_count; ++i) {
        const patch *current = &plan->patches[i];
        uint64_t start =
            current->offset > position ? current->offset : position;
        uint64_t stop = current->offset + current->length < end
                            ? current->offset + current->length
                            : end;
        if (start >= stop)
            continue;
        unsigned char *target = bytes + (start - position);
        if (current->bytes == NULL)
            memset(target, 0, (size_t)(stop - start));
        else
            memcpy(target, current->bytes + (start - current->offset),
                   (size_t)(stop - start));
    }
}

static photoc_jpeg_edit_result copy_with_patches(FILE *source, FILE *output,
                                                 uint64_t size,
                                                 const edit_plan *plan)
{
    unsigned char *buffer = malloc(SCRUB_CHUNK);
    if (buffer == NULL)
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    photoc_jpeg_edit_result result = PHOTOC_JPEG_EDIT_OK;
    if (fseeko(source, 0, SEEK_SET) != 0)
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
    for (uint64_t position = 0;
         result == PHOTOC_JPEG_EDIT_OK && position < size;) {
        size_t length = size - position < SCRUB_CHUNK
                            ? (size_t)(size - position)
                            : SCRUB_CHUNK;
        errno = 0;
        if (fread(buffer, 1, length, source) != length) {
            if (errno == 0)
                errno = EIO;
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
            break;
        }
        apply_patches(plan, position, buffer, length);
        if (fwrite(buffer, 1, length, output) != length) {
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
            break;
        }
        position += length;
    }
    free(buffer);
    return result;
}

/* Compare the written copy with the source plus patches, byte for byte. */
static photoc_jpeg_edit_result
verify_bytes(FILE *source, FILE *written, uint64_t size, const edit_plan *plan)
{
    unsigned char *expected = malloc(SCRUB_CHUNK);
    unsigned char *actual = malloc(SCRUB_CHUNK);
    photoc_jpeg_edit_result result = PHOTOC_JPEG_EDIT_OK;
    if (expected == NULL || actual == NULL)
        result = PHOTOC_JPEG_EDIT_NO_MEMORY;
    else if (fseeko(source, 0, SEEK_SET) != 0 ||
             fseeko(written, 0, SEEK_SET) != 0)
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
    for (uint64_t position = 0;
         result == PHOTOC_JPEG_EDIT_OK && position < size;) {
        size_t length = size - position < SCRUB_CHUNK
                            ? (size_t)(size - position)
                            : SCRUB_CHUNK;
        errno = 0;
        if (fread(expected, 1, length, source) != length ||
            fread(actual, 1, length, written) != length) {
            if (errno == 0)
                errno = EIO;
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
            break;
        }
        apply_patches(plan, position, expected, length);
        if (memcmp(expected, actual, length) != 0) {
            errno = EIO;
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        }
        position += length;
    }
    if (result == PHOTOC_JPEG_EDIT_OK && fgetc(written) != EOF) {
        errno = EIO;
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    free(expected);
    free(actual);
    return result;
}

typedef struct {
    bool privacy;
    bool remaining;
} residue_check;

static bool find_residue(photoc_tiff_reader *reader,
                         const photoc_tiff_entry *entry, void *data)
{
    (void)reader;
    residue_check *check = data;
    scan_entry probe = {.tag = entry->tag, .kind = entry->kind};
    if (entry->kind == PHOTOC_TIFF_GPS ||
        (entry->kind == PHOTOC_TIFF_IMAGE &&
         entry->tag == EXIF_TAG_GPS_INFO_IFD_POINTER) ||
        (check->privacy && is_identifier(&probe)))
        check->remaining = true;
    return true;
}

/* The copy must still be a valid Sony ARW whose metadata loads, with no GPS
   directory or removed identifier left anywhere in its directory graph. */
static photoc_jpeg_edit_result verify_structure(FILE *written, uint64_t size,
                                                bool privacy)
{
    residue_check check = {.privacy = privacy};
    errno = 0;
    if (photoc_tiff_walk(written, size, find_residue, &check) != 0)
        return errno == ENOMEM ? PHOTOC_JPEG_EDIT_NO_MEMORY
                               : PHOTOC_JPEG_EDIT_INVALID_ARW;
    if (check.remaining)
        return PHOTOC_JPEG_EDIT_INVALID_ARW;
    Photo photo = {0};
    photoc_metadata_result loaded =
        photoc_arw_load_metadata(written, size, &photo);
    bool gps = photo.has_gps;
    photo_cleanup(&photo);
    if (loaded == PHOTOC_METADATA_NO_MEMORY)
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    return loaded == PHOTOC_METADATA_OK && !gps ? PHOTOC_JPEG_EDIT_OK
                                                : PHOTOC_JPEG_EDIT_INVALID_ARW;
}

static bool same_source(const struct stat *left, const struct stat *right)
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

static photoc_jpeg_edit_result publish(scrub_context *context,
                                       const struct stat *before,
                                       const char *destination, bool privacy)
{
    static const char suffix[] = ".photoc-XXXXXX";
    size_t length = strlen(destination);
    if (length > SIZE_MAX - sizeof(suffix))
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    char *temporary = malloc(length + sizeof(suffix));
    if (temporary == NULL)
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    memcpy(temporary, destination, length);
    memcpy(temporary + length, suffix, sizeof(suffix));

    photoc_jpeg_edit_result result = PHOTOC_JPEG_EDIT_OK;
    int saved_errno = 0;
    int descriptor = mkstemp(temporary);
    bool created = descriptor >= 0;
    FILE *output = created ? fdopen(descriptor, "w+b") : NULL;
    if (output == NULL) {
        saved_errno = errno;
        if (created)
            close(descriptor);
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    if (result == PHOTOC_JPEG_EDIT_OK)
        result = copy_with_patches(context->file, output, context->size,
                                   &context->plan);
    if (result == PHOTOC_JPEG_EDIT_OK &&
        (fflush(output) != 0 || fsync(fileno(output)) != 0))
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
    if (result == PHOTOC_JPEG_EDIT_OK)
        result =
            verify_bytes(context->file, output, context->size, &context->plan);
    if (result == PHOTOC_JPEG_EDIT_OK)
        result = verify_structure(output, context->size, privacy);
    if (result == PHOTOC_JPEG_EDIT_IO_ERROR && saved_errno == 0)
        saved_errno = errno == 0 ? EIO : errno;
    if (output != NULL && fclose(output) != 0 &&
        result == PHOTOC_JPEG_EDIT_OK) {
        saved_errno = errno;
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
    }
    struct stat after;
    if (result == PHOTOC_JPEG_EDIT_OK) {
        if (fstat(fileno(context->file), &after) != 0) {
            saved_errno = errno;
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        } else if (!same_source(before, &after)) {
            result = PHOTOC_JPEG_EDIT_UNSAFE_SOURCE;
        }
    }
    if (result == PHOTOC_JPEG_EDIT_OK) {
        if (photoc_fs_rename_noreplace(temporary, destination) != 0) {
            saved_errno = errno;
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        } else {
            created = false;
        }
    }
    if (created)
        unlink(temporary);
    free(temporary);
    if (result == PHOTOC_JPEG_EDIT_IO_ERROR)
        errno = saved_errno;
    return result;
}

photoc_jpeg_edit_result photoc_arw_scrub_copy(const char *source_path,
                                              const char *destination_path,
                                              bool privacy,
                                              photoc_scrub_report *report)
{
    if (source_path == NULL || source_path[0] == '\0' ||
        destination_path == NULL || destination_path[0] == '\0' ||
        report == NULL)
        return PHOTOC_JPEG_EDIT_INVALID_ARGUMENT;
    *report = (photoc_scrub_report){0};
    scrub_context context = {0};
    if (photoc_fs_open_regular(source_path, &context.file, &context.size) != 0)
        return PHOTOC_JPEG_EDIT_IO_ERROR;
    struct stat before;
    photoc_jpeg_edit_result result = PHOTOC_JPEG_EDIT_OK;
    int saved_errno = 0;
    unsigned char header[2];
    if (fstat(fileno(context.file), &before) != 0 || before.st_size < 0) {
        result = PHOTOC_JPEG_EDIT_IO_ERROR;
    } else if (!read_at(context.file, 0, header, 2)) {
        result = PHOTOC_JPEG_EDIT_INVALID_ARW;
    } else {
        /* Copy, verify, and same_source all use this size snapshot. */
        context.size = (uint64_t)before.st_size;
        context.order =
            header[0] == 'M' ? EXIF_BYTE_ORDER_MOTOROLA : EXIF_BYTE_ORDER_INTEL;
        Photo photo = {0};
        photoc_metadata_result loaded =
            photoc_arw_load_metadata(context.file, context.size, &photo);
        photo_cleanup(&photo);
        if (loaded == PHOTOC_METADATA_NO_MEMORY)
            result = PHOTOC_JPEG_EDIT_NO_MEMORY;
        else if (loaded == PHOTOC_METADATA_IO_ERROR)
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        else if (loaded != PHOTOC_METADATA_OK)
            result = PHOTOC_JPEG_EDIT_INVALID_ARW;
    }
    if (result == PHOTOC_JPEG_EDIT_OK) {
        errno = 0;
        if (photoc_tiff_walk(context.file, context.size, collect_entry,
                             &context.list) != 0)
            result = errno == ENOMEM ? PHOTOC_JPEG_EDIT_NO_MEMORY
                                     : PHOTOC_JPEG_EDIT_INVALID_ARW;
    }
    if (result == PHOTOC_JPEG_EDIT_OK)
        result = plan_edits(&context, privacy, report);
    if (result == PHOTOC_JPEG_EDIT_OK && !plan_is_safe(&context.plan))
        result = PHOTOC_JPEG_EDIT_UNSAFE_ARW_LAYOUT;
    report->changed =
        result == PHOTOC_JPEG_EDIT_OK && context.plan.patch_count != 0;
    if (report->changed) {
        bool exists = false;
        if (photoc_fs_exists(destination_path, &exists) != 0) {
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        } else if (exists) {
            errno = EEXIST;
            result = PHOTOC_JPEG_EDIT_IO_ERROR;
        } else {
            result = publish(&context, &before, destination_path, privacy);
        }
    }
    if (result == PHOTOC_JPEG_EDIT_IO_ERROR)
        saved_errno = errno;
    if (result != PHOTOC_JPEG_EDIT_OK)
        report->changed = false;
    free(context.list.entries);
    free_plan(&context.plan);
    fclose(context.file);
    if (result == PHOTOC_JPEG_EDIT_IO_ERROR)
        errno = saved_errno;
    return result;
}
