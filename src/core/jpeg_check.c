#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "photoc/jpeg_check.h"

#include "jpeg.h"
#include "jpeg_metadata_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <jpeglib.h>
#include <jerror.h>

typedef struct {
    struct jpeg_error_mgr base; /* First member for callback casts. */
    jmp_buf recovery;
    int fatal;
    bool warning;
    bool truncated;
    bool limited;
} check_error;

typedef struct {
    struct jpeg_source_mgr base; /* First member for callback casts. */
    FILE *file;                  /* Borrowed; owned by the outer audit. */
    uint64_t remaining;
    unsigned char bytes[4096];
    bool end_injected;
} check_source;

typedef struct {
    struct jpeg_decompress_struct codec;
    check_error error;
    struct jpeg_progress_mgr progress;
    check_source source;
    unsigned char *row; /* Owned, one decoded scanline. */
} check_decoder;

typedef struct {
    bool malformed;
    bool no_memory;
} exif_check;

static photoc_check_result finding(photoc_check_status status,
                                   photoc_check_code code, int system_errno)
{
    return (photoc_check_result){status, code, system_errno};
}

static void codec_error(j_common_ptr codec)
{
    check_error *error = (check_error *)codec->err;
    error->fatal = codec->err->msg_code;
    longjmp(error->recovery, 1);
}

static void codec_message(j_common_ptr codec, int level)
{
    if (level < 0) {
        check_error *error = (check_error *)codec->err;
        error->warning = true;
        if (codec->err->msg_code == JWRN_JPEG_EOF ||
            codec->err->msg_code == JWRN_HIT_MARKER) {
            error->truncated = true;
        }
    }
}

static void codec_progress(j_common_ptr codec)
{
    j_decompress_ptr decoder = (j_decompress_ptr)codec;
    if (decoder->input_scan_number > (int)PHOTOC_CHECK_MAX_SCANS) {
        check_error *error = (check_error *)codec->err;
        error->limited = true;
        longjmp(error->recovery, 1);
    }
}

static void source_noop(j_decompress_ptr codec)
{
    (void)codec;
}

/* A fixed streaming buffer and original-size budget prevent concurrent file
   growth from causing unlimited reads. As libjpeg's stdio source does, signal
   premature EOF before offering a recovery EOI; further reads fail fatally. */
static boolean source_fill(j_decompress_ptr codec)
{
    check_source *source = (check_source *)codec->src;
    size_t wanted = source->remaining < sizeof(source->bytes)
                        ? (size_t)source->remaining
                        : sizeof(source->bytes);
    size_t size =
        wanted == 0 ? 0 : fread(source->bytes, 1, wanted, source->file);
    source->remaining -= size;
    if (size == 0) {
        check_error *error = (check_error *)codec->err;
        if (source->end_injected) {
            error->fatal = JERR_INPUT_EOF;
            longjmp(error->recovery, 1);
        }
        error->warning = true;
        error->truncated = true;
        source->bytes[0] = 0xff;
        source->bytes[1] = 0xd9;
        size = 2;
        source->end_injected = true;
    }
    source->base.next_input_byte = source->bytes;
    source->base.bytes_in_buffer = size;
    return TRUE;
}

static void source_skip(j_decompress_ptr codec, long count)
{
    if (count <= 0)
        return;
    while ((unsigned long)count > codec->src->bytes_in_buffer) {
        count -= (long)codec->src->bytes_in_buffer;
        source_fill(codec);
    }
    codec->src->next_input_byte += (size_t)count;
    codec->src->bytes_in_buffer -= (size_t)count;
}

static int inspect_exif(unsigned char marker, const unsigned char *payload,
                        unsigned int length, bool after_scan, void *user_data)
{
    (void)after_scan;
    exif_check *check = user_data;
    if (marker == 0xe1 && length >= 4 && memcmp(payload, "Exif", 4) == 0) {
        photoc_jpeg_edit_result result =
            photoc_jpeg_exif_audit_segment(payload, length);
        if (result == PHOTOC_JPEG_EDIT_NO_MEMORY) {
            check->no_memory = true;
            return -1;
        }
        check->malformed |= result != PHOTOC_JPEG_EDIT_OK;
    }
    return 0;
}

static bool coefficient_limit(const struct jpeg_decompress_struct *codec)
{
    uint64_t bytes = 0;
    for (int i = 0; i < codec->num_components; ++i) {
        const jpeg_component_info *component = &codec->comp_info[i];
        /* Include MCU padding, which can extend the coefficient arrays. */
        uint64_t width = (uint64_t)component->width_in_blocks + 4;
        uint64_t height = (uint64_t)component->height_in_blocks + 4;
        if (width >
            PHOTOC_CHECK_MAX_COEFFICIENT_BYTES / sizeof(JBLOCK) / height) {
            return true;
        }
        uint64_t component_bytes = width * height * sizeof(JBLOCK);
        if (component_bytes > PHOTOC_CHECK_MAX_COEFFICIENT_BYTES - bytes) {
            return true;
        }
        bytes += component_bytes;
    }
    return false;
}

static photoc_check_result fatal_result(const check_error *error,
                                        bool structure_ok)
{
    if (error->limited || error->fatal == JERR_OUT_OF_MEMORY ||
        error->fatal == JERR_IMAGE_TOO_BIG ||
        error->fatal == JERR_NO_BACKING_STORE) {
        return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_RESOURCE_LIMIT, 0);
    }
    if (error->fatal == JERR_SOF_UNSUPPORTED ||
        error->fatal == JERR_BAD_PRECISION || error->fatal == JERR_NOTIMPL ||
        error->fatal == JERR_CONVERSION_NOTIMPL ||
        error->fatal == JERR_CCIR601_NOTIMPL ||
        error->fatal == JERR_FRACT_SAMPLE_NOTIMPL) {
        return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_UNSUPPORTED_JPEG, 0);
    }
    if (error->fatal == JERR_INPUT_EOF || error->truncated) {
        return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_TRUNCATED_JPEG, 0);
    }
    return finding(PHOTOC_CHECK_ERROR,
                   structure_ok ? PHOTOC_CHECK_DECODE_FAILED
                                : PHOTOC_CHECK_INVALID_STRUCTURE,
                   0);
}

/* The mutable decoder and setjmp recovery state live on the heap so cleanup
   never reads automatic variables made indeterminate by longjmp. */
static photoc_check_result decode_stream(FILE *file, bool structure_ok,
                                         uint64_t byte_limit)
{
    check_decoder *decoder = calloc(1, sizeof(*decoder));
    if (decoder == NULL) {
        return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_INTERNAL_ERROR, ENOMEM);
    }
    decoder->codec.err = jpeg_std_error(&decoder->error.base);
    decoder->error.base.error_exit = codec_error;
    decoder->error.base.emit_message = codec_message;
    photoc_check_result result;
    if (setjmp(decoder->error.recovery) != 0) {
        result = fatal_result(&decoder->error, structure_ok);
        goto cleanup;
    }
    jpeg_create_decompress(&decoder->codec);
    decoder->codec.mem->max_memory_to_use =
        (long)PHOTOC_CHECK_MAX_COEFFICIENT_BYTES;
    decoder->progress.progress_monitor = codec_progress;
    decoder->codec.progress = &decoder->progress;
    decoder->source.file = file;
    decoder->source.remaining = byte_limit;
    decoder->source.base.init_source = source_noop;
    decoder->source.base.fill_input_buffer = source_fill;
    decoder->source.base.skip_input_data = source_skip;
    decoder->source.base.resync_to_restart = jpeg_resync_to_restart;
    decoder->source.base.term_source = source_noop;
    decoder->codec.src = &decoder->source.base;
    if (jpeg_read_header(&decoder->codec, TRUE) != JPEG_HEADER_OK) {
        result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_INVALID_STRUCTURE, 0);
        goto cleanup;
    }
    if (decoder->codec.num_components < 1 ||
        decoder->codec.num_components > 4) {
        result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_UNSUPPORTED_JPEG, 0);
        goto cleanup;
    }
    uint64_t pixels = (uint64_t)decoder->codec.image_width *
                      (uint64_t)decoder->codec.image_height;
    if (pixels == 0 || pixels > PHOTOC_CHECK_MAX_PIXELS ||
        (jpeg_has_multiple_scans(&decoder->codec) &&
         coefficient_limit(&decoder->codec))) {
        result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_RESOURCE_LIMIT, 0);
        goto cleanup;
    }
    if (!jpeg_start_decompress(&decoder->codec)) {
        result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_DECODE_FAILED, 0);
        goto cleanup;
    }
    if (decoder->codec.output_components < 1 ||
        decoder->codec.output_components > 4 ||
        decoder->codec.output_width >
            SIZE_MAX / (size_t)decoder->codec.output_components) {
        result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_RESOURCE_LIMIT, 0);
        goto cleanup;
    }
    size_t row_bytes = (size_t)decoder->codec.output_width *
                       (size_t)decoder->codec.output_components;
    decoder->row = malloc(row_bytes);
    if (decoder->row == NULL) {
        result =
            finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_INTERNAL_ERROR, ENOMEM);
        goto cleanup;
    }
    while (decoder->codec.output_scanline < decoder->codec.output_height) {
        JSAMPROW row = decoder->row;
        if (jpeg_read_scanlines(&decoder->codec, &row, 1) != 1) {
            result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_DECODE_FAILED, 0);
            goto cleanup;
        }
    }
    if (!jpeg_finish_decompress(&decoder->codec)) {
        result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_DECODE_FAILED, 0);
    } else if (decoder->error.warning) {
        result = finding(PHOTOC_CHECK_WARNING,
                         decoder->error.truncated ? PHOTOC_CHECK_TRUNCATED_JPEG
                                                  : PHOTOC_CHECK_JPEG_WARNING,
                         0);
    } else if (!structure_ok) {
        result =
            finding(PHOTOC_CHECK_WARNING, PHOTOC_CHECK_STRUCTURE_WARNING, 0);
    } else {
        result = finding(PHOTOC_CHECK_OK, PHOTOC_CHECK_READABLE, 0);
    }
cleanup:
    jpeg_destroy_decompress(&decoder->codec);
    free(decoder->row);
    free(decoder);
    return result;
}

static bool same_file(const struct stat *before, const struct stat *after)
{
#if defined(__APPLE__)
    return before->st_size == after->st_size &&
           before->st_mtimespec.tv_sec == after->st_mtimespec.tv_sec &&
           before->st_mtimespec.tv_nsec == after->st_mtimespec.tv_nsec &&
           before->st_ctimespec.tv_sec == after->st_ctimespec.tv_sec &&
           before->st_ctimespec.tv_nsec == after->st_ctimespec.tv_nsec;
#else
    return before->st_size == after->st_size &&
           before->st_mtim.tv_sec == after->st_mtim.tv_sec &&
           before->st_mtim.tv_nsec == after->st_mtim.tv_nsec &&
           before->st_ctim.tv_sec == after->st_ctim.tv_sec &&
           before->st_ctim.tv_nsec == after->st_ctim.tv_nsec;
#endif
}

photoc_check_result photoc_jpeg_check_file(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_INTERNAL_ERROR, EINVAL);
    }
    int descriptor = open(path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW);
    if (descriptor < 0) {
        return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_IO_ERROR, errno);
    }
    struct stat before;
    if (fstat(descriptor, &before) != 0) {
        int saved_errno = errno;
        close(descriptor);
        return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_IO_ERROR, saved_errno);
    }
    if (!S_ISREG(before.st_mode)) {
        close(descriptor);
        return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_IO_ERROR, EINVAL);
    }
    if (before.st_size < 0 ||
        (uintmax_t)before.st_size > PHOTOC_CHECK_MAX_FILE_BYTES) {
        close(descriptor);
        return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_RESOURCE_LIMIT, 0);
    }
    FILE *file = fdopen(descriptor, "rb");
    if (file == NULL) {
        int saved_errno = errno;
        close(descriptor);
        return finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_IO_ERROR, saved_errno);
    }
    photoc_check_result result;
    if (before.st_size == 0) {
        result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_EMPTY_FILE, 0);
    } else if (fgetc(file) != 0xff || fgetc(file) != 0xd8) {
        result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_INVALID_SIGNATURE, 0);
    } else if (fseeko(file, 0, SEEK_SET) != 0) {
        result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_IO_ERROR, errno);
    } else {
        photoc_jpeg_info info;
        exif_check exif = {0};
        errno = 0;
        bool structure_ok =
            photoc_jpeg_inspect_app_limited(file, &info, inspect_exif, &exif,
                                            (uint64_t)before.st_size) == 0;
        int structure_errno = errno;
        if (exif.no_memory) {
            result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_INTERNAL_ERROR,
                             ENOMEM);
        } else if (!structure_ok && structure_errno != EINVAL) {
            result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_IO_ERROR,
                             structure_errno == 0 ? EIO : structure_errno);
        } else if (fseeko(file, 0, SEEK_SET) != 0) {
            result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_IO_ERROR, errno);
        } else if (info.scan_count > PHOTOC_CHECK_MAX_SCANS) {
            result =
                finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_RESOURCE_LIMIT, 0);
        } else {
            result =
                decode_stream(file, structure_ok, (uint64_t)before.st_size);
            if (result.status == PHOTOC_CHECK_OK && exif.malformed) {
                result = finding(PHOTOC_CHECK_WARNING,
                                 PHOTOC_CHECK_MALFORMED_EXIF, 0);
            } else if (result.status == PHOTOC_CHECK_OK &&
                       (info.exif_count > 1 || info.exif_after_scan)) {
                result =
                    finding(PHOTOC_CHECK_WARNING, PHOTOC_CHECK_UNUSUAL_EXIF, 0);
            }
        }
    }
    if (ferror(file)) {
        result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_IO_ERROR,
                         errno == 0 ? EIO : errno);
    }
    struct stat after;
    if (fstat(fileno(file), &after) != 0) {
        result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_IO_ERROR, errno);
    } else if (!same_file(&before, &after)) {
        result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_FILE_CHANGED, 0);
    }
    if (fclose(file) != 0) {
        result = finding(PHOTOC_CHECK_ERROR, PHOTOC_CHECK_IO_ERROR, errno);
    }
    return result;
}

const char *photoc_check_status_name(photoc_check_status status)
{
    switch (status) {
    case PHOTOC_CHECK_OK:
        return "ok";
    case PHOTOC_CHECK_WARNING:
        return "warning";
    case PHOTOC_CHECK_ERROR:
        return "error";
    }
    return "error";
}

const char *photoc_check_code_name(photoc_check_code code)
{
    switch (code) {
    case PHOTOC_CHECK_READABLE:
        return "readable";
    case PHOTOC_CHECK_EMPTY_FILE:
        return "empty_file";
    case PHOTOC_CHECK_INVALID_SIGNATURE:
        return "invalid_signature";
    case PHOTOC_CHECK_INVALID_STRUCTURE:
        return "invalid_structure";
    case PHOTOC_CHECK_TRUNCATED_JPEG:
        return "truncated_jpeg";
    case PHOTOC_CHECK_DECODE_FAILED:
        return "decode_failed";
    case PHOTOC_CHECK_JPEG_WARNING:
        return "jpeg_warning";
    case PHOTOC_CHECK_STRUCTURE_WARNING:
        return "structure_warning";
    case PHOTOC_CHECK_MALFORMED_EXIF:
        return "malformed_exif";
    case PHOTOC_CHECK_UNUSUAL_EXIF:
        return "unusual_exif";
    case PHOTOC_CHECK_IO_ERROR:
        return "io_error";
    case PHOTOC_CHECK_RESOURCE_LIMIT:
        return "resource_limit";
    case PHOTOC_CHECK_UNSUPPORTED_JPEG:
        return "unsupported_jpeg";
    case PHOTOC_CHECK_FILE_CHANGED:
        return "file_changed";
    case PHOTOC_CHECK_INTERNAL_ERROR:
        return "internal_error";
    }
    return "internal_error";
}

const char *photoc_check_message(photoc_check_result result)
{
    switch (result.code) {
    case PHOTOC_CHECK_READABLE:
        return "JPEG structure and pixels are readable";
    case PHOTOC_CHECK_EMPTY_FILE:
        return "empty file";
    case PHOTOC_CHECK_INVALID_SIGNATURE:
        return "invalid JPEG signature";
    case PHOTOC_CHECK_INVALID_STRUCTURE:
        return "invalid JPEG structure; decoding failed";
    case PHOTOC_CHECK_TRUNCATED_JPEG:
        return result.status == PHOTOC_CHECK_WARNING
                   ? "decoder recovered from truncated JPEG data; image may be "
                     "incomplete"
                   : "truncated JPEG data prevented decoding";
    case PHOTOC_CHECK_DECODE_FAILED:
        return "fatal JPEG decode failure";
    case PHOTOC_CHECK_JPEG_WARNING:
        return "JPEG decoded with a recoverable decoder warning";
    case PHOTOC_CHECK_STRUCTURE_WARNING:
        return "unusual JPEG structure; pixels still decode";
    case PHOTOC_CHECK_MALFORMED_EXIF:
        return "malformed or unreadable EXIF; pixels still decode";
    case PHOTOC_CHECK_UNUSUAL_EXIF:
        return "unusual EXIF marker layout; pixels still decode";
    case PHOTOC_CHECK_IO_ERROR:
        return "unable to read file";
    case PHOTOC_CHECK_RESOURCE_LIMIT:
        return "not fully checked: JPEG exceeds resource limits or decoder "
               "memory is unavailable";
    case PHOTOC_CHECK_UNSUPPORTED_JPEG:
        return "not fully checked: unsupported JPEG coding or precision";
    case PHOTOC_CHECK_FILE_CHANGED:
        return "not reliably checked: file changed during reading";
    case PHOTOC_CHECK_INTERNAL_ERROR:
        return "unable to complete JPEG check";
    }
    return "unable to complete JPEG check";
}
