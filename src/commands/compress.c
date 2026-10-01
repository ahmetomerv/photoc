#include "photoc/commands.h"

#include "photoc/error.h"
#include "photoc/exit_codes.h"
#include "photoc/fs.h"
#include "photoc/image.h"
#include "photoc/jpeg_write.h"
#include "photoc/jpeg_metadata.h"
#include "photoc/quality_search.h"
#include "photoc/size.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char **paths;
    size_t count;
    size_t capacity;
    size_t skipped;
    int error;
    const char *output_dir;
    const char *input_dir;
    const photoc_output *output; /* Borrowed for the synchronous walk. */
    photoc_progress *progress;   /* Borrowed; caller thread only. */
} compress_walk;

typedef struct {
    const photoc_image *image;
    uint64_t metadata_overhead;
    photoc_image_result error;
    bool grayscale;
} quality_probe_context;

static int probe_quality(int quality, uint64_t *size, void *user_data)
{
    quality_probe_context *context = user_data;
    photoc_jpeg_buffer encoded = {0};
    context->error =
        context->grayscale
            ? photoc_image_encode_jpeg_grayscale(context->image, quality,
                                                 &encoded)
            : photoc_image_encode_jpeg(context->image, quality, &encoded);
    if (context->error != PHOTOC_IMAGE_OK) {
        errno = EIO;
        return -1;
    }
    if (encoded.size > UINT64_MAX - context->metadata_overhead) {
        photoc_jpeg_buffer_cleanup(&encoded);
        errno = EOVERFLOW;
        return -1;
    }
    *size = (uint64_t)encoded.size + context->metadata_overhead;
    photoc_jpeg_buffer_cleanup(&encoded);
    return 0;
}

static bool same_directory_name(const char *left, const char *right)
{
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    while (left_length > 1 && left[left_length - 1] == '/') {
        --left_length;
    }
    while (right_length > 1 && right[right_length - 1] == '/') {
        --right_length;
    }
    return left_length == right_length &&
           strncmp(left, right, left_length) == 0;
}

static bool is_compressed_output(const char *path)
{
    static const char suffix[] = ".compressed";
    const char *dot = strrchr(path, '.');
    size_t length = sizeof(suffix) - 1;
    return dot != NULL && (size_t)(dot - path) >= length &&
           memcmp(dot - length, suffix, length) == 0;
}

/* Insert before the JPEG extension so the output remains a .jpg/.jpeg file.
   The caller owns and frees *out. */
static int output_path(const char *source, char **out)
{
    static const char suffix[] = ".compressed";
    size_t length = strlen(source);
    size_t extra = sizeof(suffix) - 1;
    if (length > SIZE_MAX - extra - 1) {
        errno = EOVERFLOW;
        return -1;
    }
    const char *dot = strrchr(source, '.');
    if (dot == NULL) {
        errno = EINVAL;
        return -1;
    }
    size_t prefix = (size_t)(dot - source);
    char *name = malloc(length + extra + 1);
    if (name == NULL) {
        return -1;
    }
    memcpy(name, source, prefix);
    memcpy(name + prefix, suffix, extra);
    memcpy(name + prefix + extra, dot, length - prefix + 1);
    *out = name;
    return 0;
}

static int image_error(const char *path, photoc_image_result result)
{
    return photoc_error_image("compress", PHOTOC_ERR_NOTE_NONE, path, result,
                              errno);
}

static int jpeg_error(const char *path, photoc_jpeg_edit_result result)
{
    return photoc_error_jpeg_edit("compress", PHOTOC_ERR_NOTE_NONE, path,
                                  result, errno);
}

static void report_target_miss(const char *path,
                               const photoc_compress_options *options,
                               const photoc_quality_choice *choice)
{
    char size[PHOTOC_SIZE_TEXT_CAPACITY];
    photoc_size_format((double)choice->size, size);
    if (choice->quality == options->min_quality) {
        fprintf(stderr,
                "photoc compress: '%s': target cannot be reached without going "
                "below minimum quality %d (achieved %s)\n",
                path, options->min_quality, size);
    } else {
        fprintf(stderr,
                "photoc compress: '%s': target not met at quality %d (achieved "
                "%s)\n",
                path, choice->quality, size);
    }
}

static int ensure_output_parent(const char *destination)
{
    const char *slash = strrchr(destination, '/');
    if (slash == NULL) {
        return 0;
    }
    size_t length = slash == destination ? 1 : (size_t)(slash - destination);
    char *parent = malloc(length + 1);
    if (parent == NULL) {
        return -1;
    }
    memcpy(parent, destination, length);
    parent[length] = '\0';
    int result = photoc_fs_mkdirs(parent);
    int saved_errno = errno;
    free(parent);
    errno = saved_errno;
    return result;
}

/* 0 processed, 1 skipped because the output exists, -1 failed. */
static int compress_file(const char *path, const char *destination,
                         const photoc_compress_options *options,
                         uint64_t *original_size, uint64_t *compressed_size,
                         photoc_quality_choice *choice,
                         photoc_progress *progress)
{
    if (photoc_fs_file_size(path, original_size) != 0) {
        photoc_progress_before_diagnostic(progress);
        photoc_error_report("compress", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                            path, "unable to read file size", errno);
        return -1;
    }
    bool exists = false;
    if (photoc_fs_exists(destination, &exists) != 0) {
        photoc_progress_before_diagnostic(progress);
        photoc_error_report("compress", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                            destination, "unable to inspect output", errno);
        return -1;
    }
    if (exists) {
        return 1;
    }
    if (ensure_output_parent(destination) != 0) {
        photoc_progress_before_diagnostic(progress);
        photoc_error_report("compress", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                            destination, "cannot create destination directory",
                            errno);
        return -1;
    }

    photoc_jpeg_metadata *metadata = NULL;
    photoc_jpeg_edit_result metadata_result =
        photoc_jpeg_metadata_load_copy(path, &metadata);
    if (metadata_result != PHOTOC_JPEG_EDIT_OK) {
        photoc_progress_before_diagnostic(progress);
        jpeg_error(path, metadata_result);
        return -1;
    }

    photoc_image image = {0};
    photoc_image_result image_result = photoc_image_decode_jpeg(path, &image);
    if (image_result != PHOTOC_IMAGE_OK) {
        photoc_progress_before_diagnostic(progress);
        image_error(path, image_result);
        photoc_jpeg_metadata_free(metadata);
        return -1;
    }
    bool grayscale = photoc_jpeg_metadata_has_grayscale_icc(metadata);
    choice->quality = options->quality;
    choice->target_met = true;
    if (options->target_bytes != 0) {
        uint64_t overhead = photoc_jpeg_metadata_output_overhead(metadata);
        quality_probe_context context = {&image, overhead, PHOTOC_IMAGE_OK,
                                         grayscale};
        if (photoc_quality_search(options->target_bytes, options->min_quality,
                                  probe_quality, &context, choice) != 0) {
            photoc_progress_before_diagnostic(progress);
            if (context.error != PHOTOC_IMAGE_OK) {
                image_error(path, context.error);
            } else {
                photoc_error_report("compress", PHOTOC_ERR_NOTE_NONE,
                                    errno == ENOMEM ? PHOTOC_ERR_INTERNAL
                                                    : PHOTOC_ERR_IO,
                                    path, "quality search failed", errno);
            }
            photoc_image_cleanup(&image);
            photoc_jpeg_metadata_free(metadata);
            return -1;
        }
    }
    photoc_jpeg_buffer encoded = {0};
    image_result =
        grayscale ? photoc_image_encode_jpeg_grayscale(&image, choice->quality,
                                                       &encoded)
                  : photoc_image_encode_jpeg(&image, choice->quality, &encoded);
    photoc_image_cleanup(&image);
    if (image_result != PHOTOC_IMAGE_OK) {
        photoc_progress_before_diagnostic(progress);
        image_error(path, image_result);
        photoc_jpeg_metadata_free(metadata);
        return -1;
    }

    photoc_jpeg_edit_result write_result =
        photoc_jpeg_write_encoded_with_metadata(destination, &encoded,
                                                metadata);
    photoc_jpeg_buffer_cleanup(&encoded);
    photoc_jpeg_metadata_free(metadata);
    if (write_result != PHOTOC_JPEG_EDIT_OK) {
        if (write_result == PHOTOC_JPEG_EDIT_IO_ERROR && errno == EEXIST) {
            return 1;
        }
        photoc_progress_before_diagnostic(progress);
        jpeg_error(destination, write_result);
        return -1;
    }

    if (photoc_fs_file_size(destination, compressed_size) != 0) {
        photoc_progress_before_diagnostic(progress);
        photoc_error_report("compress", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                            destination, "cannot inspect output", errno);
        return -1;
    }
    choice->size = *compressed_size;
    if (options->target_bytes != 0) {
        choice->target_met = *compressed_size <= options->target_bytes;
    }
    return 0;
}

static bool visit_file(const char *path, photoc_fs_type type, void *user_data)
{
    compress_walk *walk = user_data;
    if (photoc_progress_interrupted()) {
        walk->error = EINTR;
        return false;
    }
    if (walk->output_dir != NULL &&
        !same_directory_name(walk->output_dir, walk->input_dir)) {
        size_t length = strlen(walk->output_dir);
        while (length > 1 && walk->output_dir[length - 1] == '/') {
            --length;
        }
        if (strncmp(path, walk->output_dir, length) == 0 &&
            (path[length] == '/' || path[length] == '\0')) {
            return true;
        }
    }
    if (type != PHOTOC_FS_FILE) {
        if (type == PHOTOC_FS_OTHER) {
            ++walk->skipped;
        }
        return true;
    }
    if (!photoc_fs_is_jpeg(path) || is_compressed_output(path)) {
        ++walk->skipped;
        photoc_output_verbose(
            walk->output, "compress",
            "skipped '%s': unsupported extension or compressed output\n", path);
        return true;
    }
    if (walk->count == walk->capacity) {
        size_t next = walk->capacity == 0 ? 16 : walk->capacity * 2;
        if (next < walk->capacity || next > SIZE_MAX / sizeof(*walk->paths)) {
            walk->error = ENOMEM;
            return false;
        }
        char **resized = realloc(walk->paths, next * sizeof(*walk->paths));
        if (resized == NULL) {
            walk->error = ENOMEM;
            return false;
        }
        walk->paths = resized;
        walk->capacity = next;
    }
    size_t length = strlen(path);
    walk->paths[walk->count] = malloc(length + 1);
    if (walk->paths[walk->count] == NULL) {
        walk->error = ENOMEM;
        return false;
    }
    memcpy(walk->paths[walk->count], path, length + 1);
    ++walk->count;
    photoc_progress_update(walk->progress, walk->count);
    return true;
}

static int compare_paths(const void *left, const void *right)
{
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

/* The caller owns and frees *destination. */
static int destination_for(const char *root, const char *source,
                           const char *output_dir, char **destination)
{
    const char *relative = source;
    if (root != NULL) {
        relative = photoc_fs_relative(root, source);
    } else if (output_dir != NULL) {
        const char *slash = strrchr(source, '/');
        relative = slash == NULL ? source : slash + 1;
    }
    char *base = NULL;
    if (output_dir != NULL &&
        photoc_fs_join(output_dir, relative, &base) != 0) {
        return -1;
    }
    int result = output_path(base == NULL ? source : base, destination);
    free(base);
    return result;
}

int photoc_command_compress_with_output(const char *path,
                                        const photoc_compress_options *options,
                                        const photoc_output *output)
{
    photoc_output_verbose(
        output, "compress", "input: '%s'; mode: %s; recursion: %s\n", path,
        options->target_bytes != 0 ? "target size" : "fixed quality",
        options->recursive ? "enabled" : "disabled");
    char size[PHOTOC_SIZE_TEXT_CAPACITY];
    photoc_size_format((double)options->target_bytes, size);
    photoc_output_verbose(output, "compress",
                          "quality: %d; minimum quality: %d; target size: %s\n",
                          options->quality, options->min_quality,
                          options->target_bytes == 0 ? "none" : size);
    bool recursive = options->recursive;
    const char *output_dir = options->output_dir;
    photoc_fs_type type;
    if (photoc_fs_get_type(path, &type) != 0) {
        return photoc_error_report("compress", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_IO, path,
                                   "unable to inspect path", errno);
    }
    if (type == PHOTOC_FS_FILE && recursive) {
        fputs("photoc compress: --recursive requires a directory\n", stderr);
        return PHOTOC_EXIT_USAGE;
    }
    if (type == PHOTOC_FS_OTHER ||
        (type == PHOTOC_FS_FILE && !photoc_fs_is_jpeg(path))) {
        return photoc_error_report(
            "compress", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_UNSUPPORTED, path,
            "expected a regular JPEG file or directory", 0);
    }
    if (output_dir != NULL && output_dir[0] == '\0') {
        fputs("photoc compress: --output-dir requires a nonempty directory\n",
              stderr);
        return PHOTOC_EXIT_USAGE;
    }
    if (output_dir != NULL && photoc_fs_mkdirs(output_dir) != 0) {
        return photoc_error_report("compress", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_IO, output_dir,
                                   "cannot create output directory", errno);
    }

    if (type == PHOTOC_FS_FILE) {
        char *destination = NULL;
        if (destination_for(NULL, path, output_dir, &destination) != 0) {
            return photoc_error_report("compress", PHOTOC_ERR_NOTE_NONE,
                                       errno == ENOMEM ? PHOTOC_ERR_INTERNAL
                                                       : PHOTOC_ERR_IO,
                                       path, "cannot build output path", errno);
        }
        uint64_t before = 0;
        uint64_t after = 0;
        photoc_quality_choice choice = {0};
        photoc_output_verbose(output, "compress", "input: '%s'; output: '%s'\n",
                              path, destination);
        int result = compress_file(path, destination, options, &before, &after,
                                   &choice, NULL);
        if (result == 0) {
            photoc_output_info(output, "Output: %s\nQuality: %d\n", destination,
                               choice.quality);
            photoc_size_format((double)before, size);
            photoc_output_info(output, "Original size: %s\n", size);
            photoc_size_format((double)after, size);
            photoc_output_info(output, "Compressed size: %s\n", size);
            if (options->target_bytes != 0) {
                photoc_size_format((double)options->target_bytes, size);
                photoc_output_info(output, "Target size: %s\n", size);
                photoc_output_info(output, "Target met: %s\n",
                                   choice.target_met ? "yes" : "no");
                if (!choice.target_met) {
                    report_target_miss(path, options, &choice);
                }
            }
            if (after <= before) {
                photoc_size_format((double)(before - after), size);
                photoc_output_info(output, "Bytes saved: %s\n", size);
            } else {
                photoc_size_format((double)(after - before), size);
                photoc_output_info(output, "Bytes saved: -%s\n", size);
            }
            photoc_output_info(output, "Percentage saved: %.2f%%\n",
                               (1.0 - (double)after / (double)before) * 100.0);
        } else if (result == 1) {
            photoc_error_report("compress", PHOTOC_ERR_NOTE_NONE,
                                PHOTOC_ERR_COLLISION, destination,
                                "output already exists", 0);
        }
        photoc_output_verbose(
            output, "compress",
            "files inspected: 1; processed: %u; failed: %u; target met: %s\n",
            result == 0 ? 1u : 0u, result != 0 ? 1u : 0u,
            choice.target_met ? "yes" : "no");
        free(destination);
        if (ferror(stdout)) {
            return photoc_error_report("compress", PHOTOC_ERR_NOTE_NONE,
                                       PHOTOC_ERR_IO, NULL,
                                       "unable to write output", EIO);
        }
        return result == 0 && choice.target_met ? PHOTOC_EXIT_SUCCESS
                                                : PHOTOC_EXIT_FAILURE;
    }

    photoc_progress *progress = output == NULL ? NULL : output->progress;
    compress_walk walk = {.output_dir = output_dir,
                          .input_dir = path,
                          .output = output,
                          .progress = progress};
    photoc_progress_set_message(progress, "Discovering photos...");
    photoc_progress_start(progress);
    int walk_result = recursive
                          ? photoc_fs_walk_recursive(path, visit_file, &walk)
                          : photoc_fs_walk(path, visit_file, &walk);
    size_t processed = 0;
    size_t skipped = walk.skipped;
    size_t failed = 0;
    size_t targets_not_met = 0;
    uint64_t before_total = 0;
    uint64_t after_total = 0;
    if (walk_result != 0) {
        photoc_progress_fail(progress, "Failed to scan directory");
        ++failed;
        photoc_error_report(
            "compress", PHOTOC_ERR_NOTE_NONE,
            (walk.error == ENOMEM) ? PHOTOC_ERR_INTERNAL : PHOTOC_ERR_IO, path,
            "cannot read directory", walk.error != 0 ? walk.error : errno);
    } else {
        photoc_progress_set_message(progress, "Compressing...");
        if (progress != NULL && progress->enabled)
            photoc_progress_set_total(progress, walk.count);
        if (walk.count > 1) {
            qsort(walk.paths, walk.count, sizeof(*walk.paths), compare_paths);
        }
        for (size_t i = 0; i < walk.count; ++i) {
            if (photoc_progress_interrupted()) {
                ++failed;
                break;
            }
            char *destination = NULL;
            if (destination_for(path, walk.paths[i], output_dir,
                                &destination) != 0) {
                ++failed;
                photoc_progress_before_diagnostic(progress);
                photoc_error_report(
                    "compress", PHOTOC_ERR_NOTE_NONE,
                    errno == ENOMEM ? PHOTOC_ERR_INTERNAL : PHOTOC_ERR_IO,
                    walk.paths[i], "cannot build output path", errno);
                photoc_progress_increment(progress);
                continue;
            }
            uint64_t before = 0;
            uint64_t after = 0;
            photoc_quality_choice choice = {0};
            photoc_output_verbose(output, "compress",
                                  "input: '%s'; output: '%s'\n", walk.paths[i],
                                  destination);
            int result = compress_file(walk.paths[i], destination, options,
                                       &before, &after, &choice, progress);
            if (result == 0) {
                ++processed;
                before_total += before;
                after_total += after;
                photoc_size_format((double)after, size);
                photoc_output_info(
                    output, "Compressed: %s -> %s (quality %d, %s)\n",
                    walk.paths[i], destination, choice.quality, size);
                if (!choice.target_met) {
                    ++targets_not_met;
                    photoc_progress_before_diagnostic(progress);
                    report_target_miss(walk.paths[i], options, &choice);
                }
            } else if (result == 1) {
                ++skipped;
                photoc_output_info(output, "Skipped: %s (output exists: %s)\n",
                                   walk.paths[i], destination);
            } else {
                ++failed;
            }
            free(destination);
            photoc_progress_increment(progress);
        }
    }
    if (photoc_progress_interrupted() && failed == 0)
        ++failed;
    if (walk_result == 0) {
        char message[96];
        if (failed != 0 || targets_not_met != 0) {
            snprintf(message, sizeof(message),
                     "Compressed %zu photos; %zu failed or missed target",
                     processed, failed + targets_not_met);
            photoc_progress_warn(progress, message);
        } else {
            snprintf(message, sizeof(message), "Compressed %zu photos",
                     processed);
            photoc_progress_finish(progress, message);
        }
    }
    photoc_output_verbose(output, "compress",
                          "JPEG files discovered: %zu; processed: %zu; "
                          "skipped: %zu; failed: %zu; targets not met: %zu\n",
                          walk.count, processed, skipped, failed,
                          targets_not_met);
    for (size_t i = 0; i < walk.count; ++i) {
        free(walk.paths[i]);
    }
    free(walk.paths);

    photoc_output_info(
        output, "Files processed: %zu\nFiles skipped: %zu\nFiles failed: %zu\n",
        processed, skipped, failed);
    if (options->target_bytes != 0) {
        photoc_output_info(output, "Targets not met: %zu\n", targets_not_met);
    }
    photoc_size_format((double)before_total, size);
    photoc_output_info(output, "Bytes before: %s\n", size);
    photoc_size_format((double)after_total, size);
    photoc_output_info(output, "Bytes after: %s\n", size);
    if (after_total <= before_total) {
        photoc_size_format((double)(before_total - after_total), size);
        photoc_output_info(output, "Total savings: %s\n", size);
    } else {
        photoc_size_format((double)(after_total - before_total), size);
        photoc_output_info(output, "Total savings: -%s\n", size);
    }
    if (ferror(stdout)) {
        return photoc_error_report("compress", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_IO, NULL,
                                   "unable to write output", EIO);
    }
    return failed == 0 && targets_not_met == 0 ? PHOTOC_EXIT_SUCCESS
                                               : PHOTOC_EXIT_FAILURE;
}

int photoc_command_compress(const char *path,
                            const photoc_compress_options *options)
{
    return photoc_command_compress_with_output(path, options, NULL);
}
