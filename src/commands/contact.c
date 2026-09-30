#include "photoc/commands.h"

#include "photoc/bitmap_font.h"
#include "photoc/error.h"
#include "photoc/exit_codes.h"
#include "photoc/fs.h"
#include "photoc/format.h"
#include "photoc/image.h"
#include "photoc/jpeg_write.h"
#include "photoc/scan.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONTACT_MAX_PHOTOS 1000u
#define CONTACT_MAX_SOURCE_BYTES UINT64_C(67108864)
#define CONTACT_MAX_CANVAS_BYTES ((size_t)67108864)
#define CONTACT_MARGIN 16u
#define CONTACT_GAP 16u
#define CONTACT_PADDING 8u

typedef struct {
    char *path;      /* Owned; released by cleanup_collection. */
    char *timestamp; /* Owned, or NULL when absent. */
    uint16_t orientation;
    uint32_t iso;
    double aperture;
    double exposure_time;
    double focal_length;
    bool has_iso;
    bool has_aperture;
    bool has_exposure_time;
    bool has_focal_length;
} contact_photo;

typedef struct {
    contact_photo *items; /* Owns every path/timestamp. */
    size_t count;
    size_t capacity;
    size_t failed;
    int error;
    const photoc_output *output; /* Borrowed during the scan. */
    photoc_progress *progress; /* Borrowed; caller thread only. */
} contact_collection;

static const char *filename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

static char *copy_text(const char *value)
{
    size_t length = strlen(value);
    if (length == SIZE_MAX)
        return NULL;
    char *copy = malloc(length + 1);
    if (copy != NULL)
        memcpy(copy, value, length + 1);
    return copy;
}

static void cleanup_collection(contact_collection *collection)
{
    for (size_t i = 0; i < collection->count; ++i) {
        free(collection->items[i].path);
        free(collection->items[i].timestamp);
    }
    free(collection->items);
    *collection = (contact_collection){0};
}

static bool collect_photo(const Photo *photo, void *user_data)
{
    contact_collection *collection = user_data;
    if (collection->count == CONTACT_MAX_PHOTOS) {
        collection->error = E2BIG;
        return false;
    }
    if (!photo->has_file_size || photo->file_size > CONTACT_MAX_SOURCE_BYTES) {
        collection->error = EFBIG;
        return false;
    }
    if (collection->count == collection->capacity) {
        size_t next = collection->capacity == 0 ? 32 : collection->capacity * 2;
        contact_photo *grown =
            realloc(collection->items, next * sizeof(*collection->items));
        if (grown == NULL) {
            collection->error = ENOMEM;
            return false;
        }
        collection->items = grown;
        collection->capacity = next;
    }
    char *path = copy_text(photo->path);
    char *timestamp = photo->capture_timestamp == NULL
                          ? NULL
                          : copy_text(photo->capture_timestamp);
    if (path == NULL ||
        (photo->capture_timestamp != NULL && timestamp == NULL)) {
        free(path);
        free(timestamp);
        collection->error = ENOMEM;
        return false;
    }
    collection->items[collection->count++] = (contact_photo){
        .path = path,
        .timestamp = timestamp,
        .orientation = photo->has_orientation ? photo->orientation : 1,
        .iso = photo->iso,
        .aperture = photo->aperture,
        .exposure_time = photo->exposure_time,
        .focal_length = photo->focal_length,
        .has_iso = photo->has_iso,
        .has_aperture = photo->has_aperture,
        .has_exposure_time = photo->has_exposure_time,
        .has_focal_length = photo->has_focal_length};
    photoc_progress_increment(collection->progress);
    return true;
}

static void scan_warning(const char *path, photoc_metadata_result reason,
                         int system_errno, void *user_data)
{
    contact_collection *collection = user_data;
    ++collection->failed;
    photoc_progress_increment(collection->progress);
    photoc_output_metadata_warning(collection->output, "contact", path, reason,
                                   system_errno);
}

static int compare_name(const void *left, const void *right)
{
    const contact_photo *a = left;
    const contact_photo *b = right;
    int order = strcmp(filename(a->path), filename(b->path));
    return order != 0 ? order : strcmp(a->path, b->path);
}

static int compare_date(const void *left, const void *right)
{
    const contact_photo *a = left;
    const contact_photo *b = right;
    if (a->timestamp == NULL && b->timestamp != NULL)
        return 1;
    if (a->timestamp != NULL && b->timestamp == NULL)
        return -1;
    if (a->timestamp != NULL) {
        int order = strcmp(a->timestamp, b->timestamp);
        if (order != 0)
            return order;
    }
    return compare_name(left, right);
}

static char *page_path(const char *output, size_t page, size_t pages)
{
    if (pages == 1)
        return copy_text(output);
    const char *dot = strrchr(output, '.');
    if (dot == NULL || page > 999)
        return NULL;
    size_t prefix = (size_t)(dot - output);
    size_t suffix = strlen(dot);
    if (prefix > SIZE_MAX - suffix - 5)
        return NULL;
    char *path = malloc(prefix + suffix + 5);
    if (path == NULL)
        return NULL;
    memcpy(path, output, prefix);
    snprintf(path + prefix, suffix + 5, "-%03zu%s", page, dot);
    return path;
}

static void cleanup_paths(char **paths, size_t count)
{
    if (paths == NULL)
        return;
    for (size_t i = 0; i < count; ++i)
        free(paths[i]);
    free(paths);
}

static int prepare_paths(const char *output, size_t pages, char ***out)
{
    char **paths = calloc(pages, sizeof(*paths));
    if (paths == NULL) {
        photoc_error_report("contact", PHOTOC_ERR_NOTE_NONE,
                            PHOTOC_ERR_INTERNAL, output,
                            "cannot allocate output paths", 0);
        return -1;
    }
    for (size_t i = 0; i < pages; ++i) {
        paths[i] = page_path(output, i + 1, pages);
        if (paths[i] == NULL) {
            photoc_error_report("contact", PHOTOC_ERR_NOTE_NONE,
                                PHOTOC_ERR_INTERNAL, output,
                                "cannot allocate output path", 0);
            cleanup_paths(paths, pages);
            return -1;
        }
        bool exists = false;
        if (photoc_fs_exists(paths[i], &exists) != 0 || exists) {
            if (exists)
                errno = EEXIST;
            photoc_error_report(
                "contact", PHOTOC_ERR_NOTE_NONE,
                exists ? PHOTOC_ERR_COLLISION : PHOTOC_ERR_IO, paths[i],
                "output already exists or is inaccessible", exists ? 0 : errno);
            cleanup_paths(paths, pages);
            return -1;
        }
    }
    *out = paths;
    return 0;
}

static bool canvas_dimensions(uint32_t columns, uint32_t thumb, size_t count,
                              bool metadata, uint32_t *width, uint32_t *height,
                              size_t *bytes)
{
    if (columns == 0 || thumb < 96 || thumb > 512 || count == 0 || count > 24)
        return false;
    uint64_t rows = (count + columns - 1) / columns;
    uint64_t cell_width = (uint64_t)thumb + 2 * CONTACT_PADDING;
    uint64_t cell_height = (uint64_t)thumb + (metadata ? 66u : 30u);
    uint64_t canvas_width =
        2 * CONTACT_MARGIN + columns * cell_width + (columns - 1) * CONTACT_GAP;
    uint64_t canvas_height =
        2 * CONTACT_MARGIN + rows * cell_height + (rows - 1) * CONTACT_GAP;
    if (canvas_width > 8192 || canvas_height > 8192 ||
        canvas_width > INT_MAX / 3 || canvas_height > INT_MAX ||
        canvas_width > SIZE_MAX / 3 ||
        canvas_height > CONTACT_MAX_CANVAS_BYTES / (canvas_width * 3))
        return false;
    *width = (uint32_t)canvas_width;
    *height = (uint32_t)canvas_height;
    *bytes = (size_t)(canvas_width * canvas_height * 3);
    return true;
}

static void rectangle(photoc_image *image, uint32_t x, uint32_t y,
                      uint32_t width, uint32_t height, unsigned char value)
{
    for (uint32_t row = y; row < y + height && row < image->height; ++row) {
        for (uint32_t col = x; col < x + width && col < image->width; ++col) {
            size_t at = (size_t)row * image->stride + (size_t)col * 3;
            image->pixels[at] = value;
            image->pixels[at + 1] = value;
            image->pixels[at + 2] = value;
        }
    }
}

static void orient_point(uint32_t x, uint32_t y, const photoc_image *source,
                         uint16_t orientation, uint32_t *raw_x, uint32_t *raw_y)
{
    uint32_t width = source->width;
    uint32_t height = source->height;
    switch (orientation) {
    case 2:
        *raw_x = width - 1 - x;
        *raw_y = y;
        break;
    case 3:
        *raw_x = width - 1 - x;
        *raw_y = height - 1 - y;
        break;
    case 4:
        *raw_x = x;
        *raw_y = height - 1 - y;
        break;
    case 5:
        *raw_x = y;
        *raw_y = x;
        break;
    case 6:
        *raw_x = y;
        *raw_y = height - 1 - x;
        break;
    case 7:
        *raw_x = width - 1 - y;
        *raw_y = height - 1 - x;
        break;
    case 8:
        *raw_x = width - 1 - y;
        *raw_y = x;
        break;
    default:
        *raw_x = x;
        *raw_y = y;
        break;
    }
}

static const unsigned char *oriented_pixel(const photoc_image *source,
                                           uint16_t orientation, uint32_t x,
                                           uint32_t y)
{
    uint32_t raw_x = 0;
    uint32_t raw_y = 0;
    orient_point(x, y, source, orientation, &raw_x, &raw_y);
    return source->pixels + (size_t)raw_y * source->stride + (size_t)raw_x * 3;
}

static uint32_t sample_coordinate(uint32_t target, uint32_t target_size,
                                  uint32_t source_size)
{
    uint64_t numerator = ((uint64_t)target * 2 + 1) * source_size * 65536u;
    int64_t coordinate =
        (int64_t)(numerator / ((uint64_t)target_size * 2)) - INT64_C(32768);
    if (coordinate < 0)
        return 0;
    uint64_t maximum = ((uint64_t)source_size - 1) * 65536u;
    return coordinate > (int64_t)maximum ? (uint32_t)maximum
                                         : (uint32_t)coordinate;
}

static void draw_thumbnail(photoc_image *canvas, const photoc_image *source,
                           uint16_t orientation, uint32_t box_x, uint32_t box_y,
                           uint32_t box_size)
{
    uint32_t oriented_width = source->width;
    uint32_t oriented_height = source->height;
    if (orientation >= 5 && orientation <= 8) {
        oriented_width = source->height;
        oriented_height = source->width;
    }
    uint32_t fitted_width = box_size;
    uint32_t fitted_height = box_size;
    if (oriented_width >= oriented_height)
        fitted_height = (uint32_t)(((uint64_t)box_size * oriented_height +
                                    oriented_width / 2) /
                                   oriented_width);
    else
        fitted_width = (uint32_t)(((uint64_t)box_size * oriented_width +
                                   oriented_height / 2) /
                                  oriented_height);
    if (fitted_width == 0)
        fitted_width = 1;
    if (fitted_height == 0)
        fitted_height = 1;
    uint32_t left = box_x + (box_size - fitted_width) / 2;
    uint32_t top = box_y + (box_size - fitted_height) / 2;
    for (uint32_t dy = 0; dy < fitted_height; ++dy) {
        uint32_t source_y =
            sample_coordinate(dy, fitted_height, oriented_height);
        uint32_t y0 = source_y >> 16;
        uint32_t y1 = y0 + (y0 + 1 < oriented_height);
        uint32_t fy = source_y & 65535u;
        for (uint32_t dx = 0; dx < fitted_width; ++dx) {
            uint32_t source_x =
                sample_coordinate(dx, fitted_width, oriented_width);
            uint32_t x0 = source_x >> 16;
            uint32_t x1 = x0 + (x0 + 1 < oriented_width);
            uint32_t fx = source_x & 65535u;
            const unsigned char *a =
                oriented_pixel(source, orientation, x0, y0);
            const unsigned char *b =
                oriented_pixel(source, orientation, x1, y0);
            const unsigned char *c =
                oriented_pixel(source, orientation, x0, y1);
            const unsigned char *d =
                oriented_pixel(source, orientation, x1, y1);
            size_t at =
                (size_t)(top + dy) * canvas->stride + (size_t)(left + dx) * 3;
            for (size_t channel = 0; channel < 3; ++channel) {
                uint64_t upper = (uint64_t)a[channel] * (65536u - fx) +
                                 (uint64_t)b[channel] * fx;
                uint64_t lower = (uint64_t)c[channel] * (65536u - fx) +
                                 (uint64_t)d[channel] * fx;
                uint64_t sample = upper * (65536u - fy) + lower * fy;
                canvas->pixels[at + channel] =
                    (unsigned char)((sample + (UINT64_C(1) << 31)) >> 32);
            }
        }
    }
}

static void append_field(char *line, size_t capacity, const char *field)
{
    size_t used = strlen(line);
    if (used >= capacity)
        return;
    if (used != 0) {
        if (capacity - used < 2)
            return;
        line[used++] = ' ';
        line[used] = '\0';
    }
    snprintf(line + used, capacity - used, "%s", field);
}

static void metadata_lines(const contact_photo *photo, char *first,
                           size_t first_size, char *second, size_t second_size)
{
    first[0] = '\0';
    second[0] = '\0';
    char field[48];
    if (photo->has_aperture) {
        snprintf(field, sizeof(field), "f/%.1f", photo->aperture);
        size_t length = strlen(field);
        if (length >= 2 && strcmp(field + length - 2, ".0") == 0)
            field[length - 2] = '\0';
        append_field(first, first_size, field);
    }
    if (photo->has_exposure_time && photo->exposure_time > 0) {
        if (photo->exposure_time < 1 && 1.0 / photo->exposure_time <= 1000000.0)
            snprintf(field, sizeof(field), "1/%.0f",
                     1.0 / photo->exposure_time);
        else
            snprintf(field, sizeof(field), "%.1fs", photo->exposure_time);
        append_field(first, first_size, field);
    }
    if (photo->has_iso) {
        snprintf(field, sizeof(field), "ISO%" PRIu32, photo->iso);
        append_field(second, second_size, field);
    }
    if (photo->has_focal_length) {
        snprintf(field, sizeof(field), "%.0fmm", photo->focal_length);
        append_field(second, second_size, field);
    }
}

static int render_page(const contact_collection *collection, size_t first,
                       size_t count, const photoc_contact_options *options,
                       const char *destination)
{
    uint32_t columns =
        options->columns < count ? options->columns : (uint32_t)count;
    uint32_t width = 0;
    uint32_t height = 0;
    size_t bytes = 0;
    if (!canvas_dimensions(columns, options->thumb_size, count,
                           options->metadata, &width, &height, &bytes)) {
        photoc_progress_before_diagnostic(collection->progress);
        return photoc_error_report(
            "contact", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_UNSUPPORTED,
            destination, "sheet dimensions exceed the 64 MiB canvas limit", 0);
    }
    photoc_image canvas = {.width = width,
                           .height = height,
                           .stride = (size_t)width * 3,
                           .pixel_bytes = bytes};
    canvas.pixels = malloc(bytes);
    if (canvas.pixels == NULL) {
        photoc_progress_before_diagnostic(collection->progress);
        return photoc_error_report("contact", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_INTERNAL, destination,
                                   "cannot allocate sheet canvas", 0);
    }
    memset(canvas.pixels, 255, bytes);
    uint32_t cell_width = options->thumb_size + 2 * CONTACT_PADDING;
    uint32_t cell_height =
        options->thumb_size + (options->metadata ? 66u : 30u);
    int outcome = PHOTOC_EXIT_SUCCESS;
    for (size_t i = 0; i < count; ++i) {
        if (photoc_progress_interrupted()) {
            outcome = PHOTOC_EXIT_FAILURE;
            break;
        }
        const contact_photo *photo = &collection->items[first + i];
        photoc_image decoded = {0};
        photoc_image_result image_result =
            photoc_image_decode_jpeg_scaled_bounded(
                photo->path, 4096, CONTACT_MAX_SOURCE_BYTES, &decoded);
        if (image_result != PHOTOC_IMAGE_OK) {
            photoc_progress_before_diagnostic(collection->progress);
            outcome = photoc_error_image("contact", PHOTOC_ERR_NOTE_NONE,
                                         photo->path, image_result, errno);
            break;
        }
        uint32_t column = (uint32_t)(i % columns);
        uint32_t row = (uint32_t)(i / columns);
        uint32_t x = CONTACT_MARGIN + column * (cell_width + CONTACT_GAP);
        uint32_t y = CONTACT_MARGIN + row * (cell_height + CONTACT_GAP);
        rectangle(&canvas, x, y, cell_width, cell_height, 246);
        rectangle(&canvas, x + CONTACT_PADDING - 1, y + CONTACT_PADDING - 1,
                  options->thumb_size + 2, options->thumb_size + 2, 220);
        rectangle(&canvas, x + CONTACT_PADDING, y + CONTACT_PADDING,
                  options->thumb_size, options->thumb_size, 238);
        draw_thumbnail(&canvas, &decoded, photo->orientation,
                       x + CONTACT_PADDING, y + CONTACT_PADDING,
                       options->thumb_size);
        photoc_image_cleanup(&decoded);
        uint32_t text_x = x + CONTACT_PADDING;
        uint32_t label_y = y + CONTACT_PADDING + options->thumb_size + 6;
        photoc_bitmap_text(&canvas, text_x, label_y, filename(photo->path),
                           options->thumb_size, 2);
        if (options->metadata) {
            char first_line[96];
            char second_line[96];
            metadata_lines(photo, first_line, sizeof(first_line), second_line,
                           sizeof(second_line));
            photoc_bitmap_text(&canvas, text_x, label_y + 18, first_line,
                               options->thumb_size, 2);
            photoc_bitmap_text(&canvas, text_x, label_y + 36, second_line,
                               options->thumb_size, 2);
        }
        photoc_progress_update(collection->progress, first + i + 1);
    }
    if (outcome == PHOTOC_EXIT_SUCCESS) {
        photoc_jpeg_buffer jpeg = {0};
        photoc_image_result image_result =
            photoc_image_encode_jpeg(&canvas, options->quality, &jpeg);
        if (image_result != PHOTOC_IMAGE_OK) {
            photoc_progress_before_diagnostic(collection->progress);
            outcome = photoc_error_image("contact", PHOTOC_ERR_NOTE_NONE,
                                         destination, image_result, errno);
        } else {
            photoc_jpeg_edit_result write_result =
                photoc_jpeg_write_encoded(destination, &jpeg, NULL);
            if (write_result != PHOTOC_JPEG_EDIT_OK) {
                photoc_progress_before_diagnostic(collection->progress);
                outcome =
                    photoc_error_jpeg_edit("contact", PHOTOC_ERR_NOTE_NONE,
                                           destination, write_result, errno);
            }
        }
        photoc_jpeg_buffer_cleanup(&jpeg);
    }
    photoc_image_cleanup(&canvas);
    return outcome;
}

int photoc_command_contact_with_output(const char *directory,
                                       const photoc_contact_options *options,
                                       const photoc_output *output)
{
    if (directory == NULL || options == NULL || options->output_path == NULL ||
        !photoc_fs_is_jpeg(options->output_path))
        return photoc_error_report(
            "contact", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_USAGE,
            options == NULL ? NULL : options->output_path,
            "output must be a .jpg or .jpeg path", 0);
    if (options->columns < 1 || options->columns > 8 ||
        options->thumb_size < 96 || options->thumb_size > 512 ||
        options->quality < 1 || options->quality > 100)
        return photoc_error_report("contact", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_USAGE, NULL,
                                   "invalid contact sheet settings", 0);
    photoc_fs_type type;
    if (photoc_fs_get_type(directory, &type) != 0)
        return photoc_error_report("contact", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_IO, directory,
                                   "unable to inspect input directory", errno);
    if (type != PHOTOC_FS_DIRECTORY)
        return photoc_error_report("contact", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_UNSUPPORTED, directory,
                                   "expected a directory", 0);
    photoc_progress *progress = output == NULL ? NULL : output->progress;
    contact_collection collection = {.output = output, .progress = progress};
    size_t total = 0;
    photoc_progress_set_message(progress, "Discovering photos...");
    if (photoc_progress_discover(progress, directory, options->recursive,
                                 PHOTOC_FORMATS_JPEG, &total) != 0) {
        photoc_progress_fail(progress, "Failed to scan directory");
        return photoc_error_report("contact", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_IO, directory,
                                   "unable to read directory", errno);
    }
    photoc_progress_set_message(progress, "Reading metadata...");
    if (progress != NULL && progress->enabled)
        photoc_progress_set_total(progress, total);
    photoc_scan_stats stats = {0};
    int scan_result = photoc_scan_directory_filtered(
        directory, options->recursive, PHOTOC_FORMATS_JPEG, collect_photo,
        scan_warning, &collection, &stats);
    if (scan_result != 0 || collection.failed != 0 || collection.count == 0) {
        photoc_progress_fail(progress, "Failed to prepare contact sheets");
        int system_errno = scan_result == 1  ? collection.error
                           : scan_result < 0 ? errno
                                             : 0;
        const char *reason =
            collection.count == 0 && scan_result == 0 && collection.failed == 0
                ? "no JPEG photos found"
            : collection.failed != 0 ? "one or more JPEG metadata loads failed"
            : collection.error == E2BIG
                ? "more than 1000 JPEGs; split the input directory"
            : collection.error == EFBIG ? "source JPEG exceeds 64 MiB"
                                        : "cannot complete directory scan";
        int status = photoc_error_report("contact", PHOTOC_ERR_NOTE_NONE,
                                         PHOTOC_ERR_METADATA, directory, reason,
                                         system_errno == 0 ? 0 : system_errno);
        cleanup_collection(&collection);
        return status;
    }
    qsort(collection.items, collection.count, sizeof(*collection.items),
          options->sort_date ? compare_date : compare_name);
    size_t capacity = options->columns * 6;
    if (capacity > 24)
        capacity = 24;
    size_t pages = (collection.count + capacity - 1) / capacity;
    char **paths = NULL;
    photoc_progress_set_message(progress, "Rendering contact sheets...");
    if (progress != NULL && progress->enabled)
        photoc_progress_set_total(progress, collection.count);
    photoc_progress_before_diagnostic(progress);
    if (prepare_paths(options->output_path, pages, &paths) != 0) {
        photoc_progress_fail(progress, "Failed to prepare contact sheets");
        cleanup_collection(&collection);
        return PHOTOC_EXIT_FAILURE;
    }
    size_t written = 0;
    for (size_t page = 0; page < pages; ++page) {
        if (photoc_progress_interrupted())
            break;
        size_t first = page * capacity;
        size_t count = collection.count - first;
        if (count > capacity)
            count = capacity;
        photoc_output_verbose(output, "contact",
                              "page %zu/%zu: %zu thumbnails -> '%s'\n",
                              page + 1, pages, count, paths[page]);
        if (render_page(&collection, first, count, options, paths[page]) != 0)
            break;
        ++written;
        photoc_output_info(output, "Contact sheet: %s (%zu photos)\n",
                           paths[page], count);
    }
    if (written == pages)
        photoc_progress_finish(progress, "Created contact sheets");
    else
        photoc_progress_fail(progress, "Failed to create contact sheets");
    photoc_output_info(output, "Sheets written: %zu; photos: %zu\n", written,
                       collection.count);
    cleanup_paths(paths, pages);
    cleanup_collection(&collection);
    return written == pages ? PHOTOC_EXIT_SUCCESS : PHOTOC_EXIT_FAILURE;
}
