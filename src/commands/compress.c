#include "photoc/commands.h"

#include "photoc/exit_codes.h"
#include "photoc/fs.h"
#include "photoc/image.h"
#include "photoc/jpeg_write.h"
#include "photoc/quality_search.h"

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
} compress_walk;

typedef struct {
    const photoc_image *image;
    uint64_t exif_overhead;
    photoc_image_result error;
} quality_probe_context;

static int probe_quality(int quality, uint64_t *size, void *user_data)
{
    quality_probe_context *context = user_data;
    photoc_jpeg_buffer encoded = {0};
    context->error = photoc_image_encode_jpeg(context->image, quality, &encoded);
    if (context->error != PHOTOC_IMAGE_OK) {
        errno = EIO;
        return -1;
    }
    if (encoded.size > UINT64_MAX - context->exif_overhead) {
        photoc_jpeg_buffer_cleanup(&encoded);
        errno = EOVERFLOW;
        return -1;
    }
    *size = (uint64_t)encoded.size + context->exif_overhead;
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
    int saved_errno = errno;
    fprintf(stderr, "photoc compress: '%s': %s", path,
            photoc_image_result_message(result));
    if (result == PHOTOC_IMAGE_IO_ERROR) {
        fprintf(stderr, ": %s", strerror(saved_errno));
    }
    fputc('\n', stderr);
    return PHOTOC_EXIT_FAILURE;
}

static int jpeg_error(const char *path, photoc_jpeg_edit_result result)
{
    int saved_errno = errno;
    fprintf(stderr, "photoc compress: '%s': %s", path,
            photoc_jpeg_edit_result_message(result));
    if (result == PHOTOC_JPEG_EDIT_IO_ERROR) {
        fprintf(stderr, ": %s", strerror(saved_errno));
    }
    fputc('\n', stderr);
    return PHOTOC_EXIT_FAILURE;
}

static void report_target_miss(const char *path,
                               const photoc_compress_options *options,
                               const photoc_quality_choice *choice)
{
    if (choice->quality == options->min_quality) {
        fprintf(stderr, "photoc compress: '%s': target cannot be reached without going below minimum quality %d (achieved %" PRIu64 " bytes)\n",
                path, options->min_quality, choice->size);
    } else {
        fprintf(stderr, "photoc compress: '%s': target not met at quality %d (achieved %" PRIu64 " bytes)\n",
                path, choice->quality, choice->size);
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
                         photoc_quality_choice *choice)
{
    if (photoc_fs_file_size(path, original_size) != 0) {
        fprintf(stderr, "photoc compress: '%s': %s\n", path, strerror(errno));
        return -1;
    }
    bool exists = false;
    if (photoc_fs_exists(destination, &exists) != 0) {
        fprintf(stderr, "photoc compress: '%s': %s\n", destination,
                strerror(errno));
        return -1;
    }
    if (exists) {
        return 1;
    }
    if (ensure_output_parent(destination) != 0) {
        fprintf(stderr, "photoc compress: cannot create destination for '%s': %s\n",
                destination, strerror(errno));
        return -1;
    }

    photoc_jpeg_exif *exif = NULL;
    photoc_jpeg_edit_result exif_result = photoc_jpeg_exif_load_copy(path, &exif);
    if (exif_result != PHOTOC_JPEG_EDIT_OK &&
        exif_result != PHOTOC_JPEG_EDIT_NO_EXIF) {
        jpeg_error(path, exif_result);
        return -1;
    }

    photoc_image image = {0};
    photoc_image_result image_result = photoc_image_decode_jpeg(path, &image);
    if (image_result != PHOTOC_IMAGE_OK) {
        image_error(path, image_result);
        photoc_jpeg_exif_free(exif);
        return -1;
    }
    choice->quality = options->quality;
    choice->target_met = true;
    if (options->target_bytes != 0) {
        uint64_t overhead = 0;
        photoc_jpeg_edit_result overhead_result =
            photoc_jpeg_exif_output_overhead(exif, &overhead);
        if (overhead_result != PHOTOC_JPEG_EDIT_OK) {
            jpeg_error(path, overhead_result);
            photoc_image_cleanup(&image);
            photoc_jpeg_exif_free(exif);
            return -1;
        }
        quality_probe_context context = {&image, overhead, PHOTOC_IMAGE_OK};
        if (photoc_quality_search(options->target_bytes,
                                  options->min_quality, probe_quality,
                                  &context, choice) != 0) {
            if (context.error != PHOTOC_IMAGE_OK) {
                image_error(path, context.error);
            } else {
                fprintf(stderr, "photoc compress: '%s': quality search failed: %s\n",
                        path, strerror(errno));
            }
            photoc_image_cleanup(&image);
            photoc_jpeg_exif_free(exif);
            return -1;
        }
    }
    photoc_jpeg_buffer encoded = {0};
    image_result = photoc_image_encode_jpeg(&image, choice->quality, &encoded);
    photoc_image_cleanup(&image);
    if (image_result != PHOTOC_IMAGE_OK) {
        image_error(path, image_result);
        photoc_jpeg_exif_free(exif);
        return -1;
    }

    photoc_jpeg_edit_result write_result = photoc_jpeg_write_encoded(
        destination, &encoded, exif);
    photoc_jpeg_buffer_cleanup(&encoded);
    photoc_jpeg_exif_free(exif);
    if (write_result != PHOTOC_JPEG_EDIT_OK) {
        if (write_result == PHOTOC_JPEG_EDIT_IO_ERROR && errno == EEXIST) {
            return 1;
        }
        jpeg_error(destination, write_result);
        return -1;
    }

    if (photoc_fs_file_size(destination, compressed_size) != 0) {
        fprintf(stderr, "photoc compress: '%s': cannot inspect output: %s\n",
                destination, strerror(errno));
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
        relative += strlen(root);
        while (*relative == '/') {
            ++relative;
        }
    } else if (output_dir != NULL) {
        const char *slash = strrchr(source, '/');
        relative = slash == NULL ? source : slash + 1;
    }
    char *base = NULL;
    if (output_dir != NULL && photoc_fs_join(output_dir, relative, &base) != 0) {
        return -1;
    }
    int result = output_path(base == NULL ? source : base, destination);
    free(base);
    return result;
}

int photoc_command_compress(const char *path,
                            const photoc_compress_options *options)
{
    bool recursive = options->recursive;
    const char *output_dir = options->output_dir;
    photoc_fs_type type;
    if (photoc_fs_get_type(path, &type) != 0) {
        fprintf(stderr, "photoc compress: '%s': %s\n", path, strerror(errno));
        return PHOTOC_EXIT_FAILURE;
    }
    if (type == PHOTOC_FS_FILE && recursive) {
        fputs("photoc compress: --recursive requires a directory\n", stderr);
        return PHOTOC_EXIT_USAGE;
    }
    if (type == PHOTOC_FS_OTHER ||
        (type == PHOTOC_FS_FILE && !photoc_fs_is_jpeg(path))) {
        fprintf(stderr, "photoc compress: '%s': expected a regular JPEG file or directory\n",
                path);
        return PHOTOC_EXIT_FAILURE;
    }
    if (output_dir != NULL && output_dir[0] == '\0') {
        fputs("photoc compress: --output-dir requires a nonempty directory\n", stderr);
        return PHOTOC_EXIT_USAGE;
    }
    if (output_dir != NULL && photoc_fs_mkdirs(output_dir) != 0) {
        fprintf(stderr, "photoc compress: cannot create output directory '%s': %s\n",
                output_dir, strerror(errno));
        return PHOTOC_EXIT_FAILURE;
    }

    if (type == PHOTOC_FS_FILE) {
        char *destination = NULL;
        if (destination_for(NULL, path, output_dir, &destination) != 0) {
            fprintf(stderr, "photoc compress: cannot build output path: %s\n",
                    strerror(errno));
            return PHOTOC_EXIT_FAILURE;
        }
        uint64_t before = 0;
        uint64_t after = 0;
        photoc_quality_choice choice = {0};
        int result = compress_file(path, destination, options, &before, &after,
                                   &choice);
        if (result == 0) {
            printf("Output: %s\nQuality: %d\n", destination, choice.quality);
            printf("Original size: %" PRIu64 " bytes\n", before);
            printf("Compressed size: %" PRIu64 " bytes\n", after);
            if (options->target_bytes != 0) {
                printf("Target size: %" PRIu64 " bytes\n", options->target_bytes);
                printf("Target met: %s\n", choice.target_met ? "yes" : "no");
                if (!choice.target_met) {
                    report_target_miss(path, options, &choice);
                }
            }
            if (after <= before) {
                printf("Bytes saved: %" PRIu64 " bytes\n", before - after);
            } else {
                printf("Bytes saved: -%" PRIu64 " bytes\n", after - before);
            }
            printf("Percentage saved: %.2f%%\n",
                   (1.0 - (double)after / (double)before) * 100.0);
        } else if (result == 1) {
            fprintf(stderr, "photoc compress: '%s': File exists\n", destination);
        }
        free(destination);
        if (ferror(stdout)) {
            fputs("photoc compress: unable to write output\n", stderr);
            return PHOTOC_EXIT_FAILURE;
        }
        return result == 0 && choice.target_met ? PHOTOC_EXIT_SUCCESS :
               PHOTOC_EXIT_FAILURE;
    }

    compress_walk walk = {.output_dir = output_dir, .input_dir = path};
    int walk_result = recursive ? photoc_fs_walk_recursive(path, visit_file, &walk) :
                                  photoc_fs_walk(path, visit_file, &walk);
    size_t processed = 0;
    size_t skipped = walk.skipped;
    size_t failed = 0;
    size_t targets_not_met = 0;
    uint64_t before_total = 0;
    uint64_t after_total = 0;
    if (walk_result != 0) {
        ++failed;
        fprintf(stderr, "photoc compress: cannot walk '%s': %s\n", path,
                strerror(walk.error != 0 ? walk.error : errno));
    } else {
        if (walk.count > 1) {
            qsort(walk.paths, walk.count, sizeof(*walk.paths), compare_paths);
        }
        for (size_t i = 0; i < walk.count; ++i) {
            char *destination = NULL;
            if (destination_for(path, walk.paths[i], output_dir, &destination) != 0) {
                ++failed;
                fprintf(stderr, "photoc compress: '%s': cannot build output path: %s\n",
                        walk.paths[i], strerror(errno));
                continue;
            }
            uint64_t before = 0;
            uint64_t after = 0;
            photoc_quality_choice choice = {0};
            int result = compress_file(walk.paths[i], destination, options,
                                       &before, &after, &choice);
            if (result == 0) {
                ++processed;
                before_total += before;
                after_total += after;
                printf("Compressed: %s -> %s (quality %d, %" PRIu64 " bytes)\n",
                       walk.paths[i], destination, choice.quality, after);
                if (!choice.target_met) {
                    ++targets_not_met;
                    report_target_miss(walk.paths[i], options, &choice);
                }
            } else if (result == 1) {
                ++skipped;
                printf("Skipped: %s (output exists: %s)\n", walk.paths[i],
                       destination);
            } else {
                ++failed;
            }
            free(destination);
        }
    }
    for (size_t i = 0; i < walk.count; ++i) {
        free(walk.paths[i]);
    }
    free(walk.paths);

    printf("Files processed: %zu\nFiles skipped: %zu\nFiles failed: %zu\n",
           processed, skipped, failed);
    if (options->target_bytes != 0) {
        printf("Targets not met: %zu\n", targets_not_met);
    }
    printf("Bytes before: %" PRIu64 "\nBytes after: %" PRIu64 "\n",
           before_total, after_total);
    if (after_total <= before_total) {
        printf("Total savings: %" PRIu64 " bytes\n", before_total - after_total);
    } else {
        printf("Total savings: -%" PRIu64 " bytes\n", after_total - before_total);
    }
    if (ferror(stdout)) {
        fputs("photoc compress: unable to write output\n", stderr);
        return PHOTOC_EXIT_FAILURE;
    }
    return failed == 0 && targets_not_met == 0 ? PHOTOC_EXIT_SUCCESS :
           PHOTOC_EXIT_FAILURE;
}
