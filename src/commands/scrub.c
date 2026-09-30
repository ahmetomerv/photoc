#include "photoc/commands.h"

#include "photoc/error.h"
#include "photoc/exit_codes.h"
#include "photoc/fs.h"
#include "photoc/jpeg_write.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t processed;
    size_t skipped;
    size_t failed;
    size_t gps_removed;
} scrub_counts;

typedef struct {
    scrub_counts counts;
    char **paths;
    size_t count;
    size_t capacity;
    int error;
    const photoc_output *output; /* Borrowed for the synchronous walk. */
    photoc_progress *progress;   /* Borrowed; caller thread only. */
} scrub_walk;

static bool is_scrubbed_output(const char *path)
{
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    if (dot == NULL || (slash != NULL && dot < slash)) {
        return false;
    }
    static const char suffix[] = ".scrubbed";
    size_t stem_length = (size_t)(dot - path);
    size_t suffix_length = sizeof(suffix) - 1;
    return stem_length >= suffix_length &&
           memcmp(dot - suffix_length, suffix, suffix_length) == 0;
}

/* Insert the suffix before .jpg/.jpeg so the result remains recognizable as
   JPEG. The caller owns *destination and frees it. */
static int scrubbed_path(const char *path, char **destination)
{
    static const char suffix[] = ".scrubbed";
    size_t length = strlen(path);
    size_t extra = sizeof(suffix) - 1;
    if (length > SIZE_MAX - extra - 1) {
        errno = EOVERFLOW;
        return -1;
    }
    const char *dot = strrchr(path, '.');
    if (dot == NULL) {
        errno = EINVAL;
        return -1;
    }
    size_t prefix = (size_t)(dot - path);
    char *result = malloc(length + extra + 1);
    if (result == NULL) {
        return -1;
    }
    memcpy(result, path, prefix);
    memcpy(result + prefix, suffix, extra);
    memcpy(result + prefix + extra, dot, length - prefix + 1);
    *destination = result;
    return 0;
}

static void report_edit_error(const char *path, photoc_jpeg_edit_result result,
                              int saved_errno)
{
    photoc_error_jpeg_edit("scrub", PHOTOC_ERR_NOTE_NONE, path, result,
                           saved_errno);
}

static void scrub_file(const char *path, bool in_place, int mode,
                       scrub_counts *counts, const photoc_output *output)
{
    photoc_progress *progress = output == NULL ? NULL : output->progress;
    if (!photoc_fs_is_jpeg(path)) {
        ++counts->skipped;
        return;
    }

    if (mode != 0) {
        char *destination = NULL;
        if (!in_place && scrubbed_path(path, &destination) != 0) {
            ++counts->failed;
            photoc_progress_before_diagnostic(progress);
            photoc_error_report("scrub", PHOTOC_ERR_NOTE_NONE,
                                errno == ENOMEM ? PHOTOC_ERR_INTERNAL
                                                : PHOTOC_ERR_IO,
                                path, "cannot build output path", errno);
            return;
        }
        photoc_scrub_report report = {0};
        photoc_jpeg_edit_result result = photoc_jpeg_scrub_metadata(
            path, in_place ? path : destination,
            mode == 1 ? PHOTOC_SCRUB_PRIVACY : PHOTOC_SCRUB_ALL_METADATA,
            in_place, &report);
        int saved_errno = errno;
        if (result == PHOTOC_JPEG_EDIT_OK && report.changed) {
            ++counts->processed;
            photoc_output_info(output, "Metadata removed%s: %s%s%s\n",
                               in_place ? " in place" : "", path,
                               in_place ? "" : " -> ",
                               in_place ? "" : destination);
            photoc_output_verbose(output, "scrub",
                                  "removed categories: EXIF GPS=%s, EXIF "
                                  "identifiers=%s, XMP=%s, IPTC=%s\n",
                                  report.exif_gps ? "yes" : "no",
                                  report.exif_identifiers ? "yes" : "no",
                                  report.xmp_fields ? "yes" : "no",
                                  report.iptc_fields ? "yes" : "no");
            if ((mode == 1 && report.maker_note) || report.unhandled_metadata) {
                photoc_output_metadata_warning(
                    output, "scrub", in_place ? path : destination,
                    PHOTOC_METADATA_PRIVACY_REMAINS, 0);
            }
        } else if (result == PHOTOC_JPEG_EDIT_OK) {
            ++counts->skipped;
            photoc_output_info(
                output, "Skipped: %s (no supported metadata to remove)\n",
                path);
            if ((mode == 1 && report.maker_note) || report.unhandled_metadata) {
                photoc_output_metadata_warning(
                    output, "scrub", path, PHOTOC_METADATA_PRIVACY_REMAINS, 0);
            }
        } else {
            ++counts->failed;
            photoc_progress_before_diagnostic(progress);
            report_edit_error(in_place ? path : destination, result,
                              saved_errno);
        }
        free(destination);
        return;
    }

    photoc_jpeg_exif *exif = NULL;
    photoc_jpeg_edit_result result = photoc_jpeg_exif_load_copy(path, &exif);
    if (result == PHOTOC_JPEG_EDIT_NO_EXIF) {
        ++counts->skipped;
        photoc_output_verbose(output, "scrub",
                              "skipped '%s': no EXIF GPS tags\n", path);
        photoc_output_info(output, "Skipped: %s (no EXIF GPS tags)\n", path);
        return;
    }
    if (result != PHOTOC_JPEG_EDIT_OK) {
        ++counts->failed;
        photoc_progress_before_diagnostic(progress);
        report_edit_error(path, result, errno);
        return;
    }
    if (!photoc_jpeg_exif_has_gps(exif)) {
        ++counts->skipped;
        photoc_output_verbose(output, "scrub",
                              "skipped '%s': no EXIF GPS tags\n", path);
        photoc_output_info(output, "Skipped: %s (no EXIF GPS tags)\n", path);
        photoc_jpeg_exif_free(exif);
        return;
    }

    char *destination = NULL;
    if (!in_place && scrubbed_path(path, &destination) != 0) {
        ++counts->failed;
        photoc_progress_before_diagnostic(progress);
        photoc_error_report("scrub", PHOTOC_ERR_NOTE_NONE,
                            errno == ENOMEM ? PHOTOC_ERR_INTERNAL
                                            : PHOTOC_ERR_IO,
                            path, "cannot build output path", errno);
        photoc_jpeg_exif_free(exif);
        return;
    }

    photoc_output_verbose(output, "scrub", "input: '%s'; output: '%s'\n", path,
                          in_place ? path : destination);
    result = photoc_jpeg_exif_remove_gps(exif);
    if (result == PHOTOC_JPEG_EDIT_OK) {
        result = in_place
                     ? photoc_jpeg_replace_with_exif(path, exif)
                     : photoc_jpeg_write_with_exif(path, destination, exif);
    }
    int saved_errno = errno;
    if (result == PHOTOC_JPEG_EDIT_OK) {
        ++counts->processed;
        ++counts->gps_removed;
        if (in_place) {
            photoc_output_info(output, "GPS removed in place: %s\n", path);
        } else {
            photoc_output_info(output, "GPS removed: %s -> %s\n", path,
                               destination);
        }
    } else {
        ++counts->failed;
        photoc_progress_before_diagnostic(progress);
        report_edit_error(in_place ? path : destination, result, saved_errno);
    }
    free(destination);
    photoc_jpeg_exif_free(exif);
}

static bool visit_file(const char *path, photoc_fs_type type, void *user_data)
{
    scrub_walk *walk = user_data;
    if (photoc_progress_interrupted()) {
        walk->error = EINTR;
        return false;
    }
    if (type == PHOTOC_FS_FILE) {
        if (!photoc_fs_is_jpeg(path) || is_scrubbed_output(path)) {
            ++walk->counts.skipped;
            photoc_output_verbose(
                walk->output, "scrub",
                "skipped '%s': unsupported extension or scrubbed output\n",
                path);
            return true;
        }
        if (walk->count == walk->capacity) {
            size_t next = walk->capacity == 0 ? 16 : walk->capacity * 2;
            if (next < walk->capacity ||
                next > SIZE_MAX / sizeof(*walk->paths)) {
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
    } else if (type == PHOTOC_FS_OTHER) {
        ++walk->counts.skipped;
    }
    return true;
}

static int compare_paths(const void *left, const void *right)
{
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

static int command_scrub(const char *path, bool recursive, bool in_place,
                         int mode, const photoc_output *output)
{
    photoc_output_verbose(
        output, "scrub", "input: '%s'; mode: %s; recursion: %s\n", path,
        in_place ? "in place" : "copy", recursive ? "enabled" : "disabled");
    photoc_fs_type type;
    if (photoc_fs_get_type(path, &type) != 0) {
        return photoc_error_report("scrub", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                   path, "unable to inspect path", errno);
    }
    if (type == PHOTOC_FS_OTHER) {
        return photoc_error_report(
            "scrub", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_UNSUPPORTED, path,
            "expected a regular JPEG file or directory", 0);
    }
    if (type == PHOTOC_FS_FILE && recursive) {
        fputs("photoc scrub: --recursive requires a directory\n", stderr);
        return PHOTOC_EXIT_USAGE;
    }
    if (type == PHOTOC_FS_FILE && !photoc_fs_is_jpeg(path)) {
        return photoc_error_report(
            "scrub", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_UNSUPPORTED, path,
            "expected a regular JPEG file or directory", 0);
    }

    scrub_counts counts = {0};
    size_t discovered = 1;
    if (type == PHOTOC_FS_FILE) {
        scrub_file(path, in_place, mode, &counts, output);
    } else {
        photoc_progress *progress = output == NULL ? NULL : output->progress;
        scrub_walk walk = {.output = output, .progress = progress};
        photoc_progress_set_message(progress, "Discovering photos...");
        photoc_progress_start(progress);
        int result = recursive
                         ? photoc_fs_walk_recursive(path, visit_file, &walk)
                         : photoc_fs_walk(path, visit_file, &walk);
        counts = walk.counts;
        discovered = walk.count;
        if (result != 0) {
            photoc_progress_fail(progress, "Failed to scan directory");
            ++counts.failed;
            photoc_error_report("scrub", PHOTOC_ERR_NOTE_NONE,
                                walk.error == ENOMEM ? PHOTOC_ERR_INTERNAL
                                                     : PHOTOC_ERR_IO,
                                path, "cannot read directory",
                                walk.error != 0 ? walk.error : errno);
        } else {
            photoc_progress_set_message(progress, "Scrubbing metadata...");
            if (progress != NULL && progress->enabled)
                photoc_progress_set_total(progress, walk.count);
            if (walk.count > 1) {
                qsort(walk.paths, walk.count, sizeof(*walk.paths),
                      compare_paths);
            }
            for (size_t i = 0; i < walk.count; ++i) {
                if (photoc_progress_interrupted()) {
                    ++counts.failed;
                    break;
                }
                scrub_file(walk.paths[i], in_place, mode, &counts, output);
                photoc_progress_increment(progress);
            }
            if (photoc_progress_interrupted() && counts.failed == 0)
                ++counts.failed;
            char message[96];
            if (counts.failed != 0) {
                snprintf(message, sizeof(message),
                         "Scrubbed %zu photos; %zu failed", counts.processed,
                         counts.failed);
                photoc_progress_warn(progress, message);
            } else {
                snprintf(message, sizeof(message), "Scrubbed %zu photos",
                         counts.processed);
                photoc_progress_finish(progress, message);
            }
        }
        for (size_t i = 0; i < walk.count; ++i) {
            free(walk.paths[i]);
        }
        free(walk.paths);
    }
    if (mode == 0) {
        photoc_output_verbose(output, "scrub",
                              "JPEG files discovered: %zu; processed: %zu; "
                              "skipped: %zu; failed: %zu; GPS removed: %zu\n",
                              discovered, counts.processed, counts.skipped,
                              counts.failed, counts.gps_removed);
        photoc_output_info(
            output,
            "Files processed: %zu\nFiles skipped: %zu\nFiles failed: %zu\n"
            "Files with GPS found and removed: %zu\n",
            counts.processed, counts.skipped, counts.failed,
            counts.gps_removed);
    } else {
        photoc_output_verbose(output, "scrub",
                              "JPEG files discovered: %zu; processed: %zu; "
                              "skipped: %zu; failed: %zu\n",
                              discovered, counts.processed, counts.skipped,
                              counts.failed);
        photoc_output_info(
            output,
            "Files processed: %zu\nFiles skipped: %zu\nFiles failed: %zu\n",
            counts.processed, counts.skipped, counts.failed);
    }
    if (ferror(stdout)) {
        return photoc_error_report("scrub", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                                   NULL, "unable to write output", EIO);
    }
    return counts.failed == 0 ? PHOTOC_EXIT_SUCCESS : PHOTOC_EXIT_FAILURE;
}

int photoc_command_scrub_with_output(const char *path, bool recursive,
                                     bool in_place, const photoc_output *output)
{
    return command_scrub(path, recursive, in_place, 0, output);
}

int photoc_command_scrub_mode_with_output(const char *path, bool recursive,
                                          bool in_place, photoc_scrub_mode mode,
                                          const photoc_output *output)
{
    return command_scrub(path, recursive, in_place,
                         mode == PHOTOC_SCRUB_PRIVACY ? 1 : 2, output);
}

int photoc_command_scrub(const char *path, bool recursive, bool in_place)
{
    return photoc_command_scrub_with_output(path, recursive, in_place, NULL);
}
