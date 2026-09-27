#define _POSIX_C_SOURCE 200809L

#include "photoc/error.h"
#include "photoc/exit_codes.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stdout, "%s:%d: %s\n", __FILE__, __LINE__, #condition);   \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

static void expect_line(int code, const char *expected, int actual_code,
                        const char *actual)
{
    CHECK(actual_code == code);
    CHECK(strcmp(actual, expected) == 0);
    if (actual_code != code || strcmp(actual, expected) != 0) {
        fprintf(stdout, "code %d (expected %d)\nactual: %s\nexpected: %s\n",
                actual_code, code, actual, expected);
    }
}

static void capture(int (*call)(void *), void *data, int *code, char *output,
                    size_t output_size)
{
    char path[] = "photoc-error-capture-XXXXXX";
    int file = mkstemp(path);
    int saved = dup(STDERR_FILENO);
    fflush(stderr);
    dup2(file, STDERR_FILENO);
    *code = call(data);
    fflush(stderr);
    dup2(saved, STDERR_FILENO);
    close(saved);
    lseek(file, 0, SEEK_SET);
    ssize_t count = read(file, output, output_size - 1);
    if (count < 0) {
        count = 0;
    }
    output[count] = '\0';
    close(file);
    unlink(path);
}

static int report_collision(void *unused)
{
    (void)unused;
    return photoc_error_report("compress", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO,
                               "out.jpg", "unable to write JPEG file", EEXIST);
}

static int report_decode(void *unused)
{
    (void)unused;
    return photoc_error_metadata("exif", PHOTOC_ERR_NOTE_NONE, "bad.jpg",
                                 PHOTOC_METADATA_INVALID_JPEG, 0);
}

static int report_warning(void *unused)
{
    (void)unused;
    return photoc_error_report("duplicates", PHOTOC_ERR_NOTE_WARNING,
                               PHOTOC_ERR_IO, "locked.bin", "unable to read file",
                               EACCES);
}

static int report_usage(void *unused)
{
    (void)unused;
    return photoc_error_report("sort", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_USAGE,
                               NULL, "invalid gap '30'", 0);
}

static int report_skipped(void *unused)
{
    (void)unused;
    return photoc_error_reportf("rename", PHOTOC_ERR_NOTE_SKIPPED,
                                PHOTOC_ERR_COLLISION, "a.jpg", 0,
                                "destination exists '%s'", "taken.jpg");
}

int main(void)
{
    CHECK(strcmp(photoc_error_kind_name(PHOTOC_ERR_USAGE), "invalid input") == 0);
    CHECK(strcmp(photoc_error_kind_name(PHOTOC_ERR_UNSUPPORTED),
                 "unsupported file") == 0);
    CHECK(strcmp(photoc_error_kind_name(PHOTOC_ERR_IO), "file I/O error") == 0);
    CHECK(strcmp(photoc_error_kind_name(PHOTOC_ERR_METADATA),
                 "metadata error") == 0);
    CHECK(strcmp(photoc_error_kind_name(PHOTOC_ERR_DECODE),
                 "image decode error") == 0);
    CHECK(strcmp(photoc_error_kind_name(PHOTOC_ERR_COLLISION), "collision") == 0);
    CHECK(strcmp(photoc_error_kind_name(PHOTOC_ERR_INTERNAL),
                 "internal error") == 0);

    CHECK(photoc_error_kind_for_metadata(PHOTOC_METADATA_UNSUPPORTED_FORMAT) ==
          PHOTOC_ERR_UNSUPPORTED);
    CHECK(photoc_error_kind_for_metadata(PHOTOC_METADATA_INVALID_JPEG) ==
          PHOTOC_ERR_DECODE);
    CHECK(photoc_error_kind_for_metadata(PHOTOC_METADATA_IO_ERROR) ==
          PHOTOC_ERR_IO);
    CHECK(photoc_error_kind_for_metadata(PHOTOC_METADATA_NO_MEMORY) ==
          PHOTOC_ERR_INTERNAL);
    CHECK(photoc_error_kind_for_image(PHOTOC_IMAGE_CODEC_ERROR) ==
          PHOTOC_ERR_DECODE);
    CHECK(photoc_error_kind_for_image(PHOTOC_IMAGE_IO_ERROR) == PHOTOC_ERR_IO);
    CHECK(photoc_error_kind_for_jpeg_edit(PHOTOC_JPEG_EDIT_IO_ERROR, EEXIST) ==
          PHOTOC_ERR_COLLISION);
    CHECK(photoc_error_kind_for_jpeg_edit(PHOTOC_JPEG_EDIT_INVALID_EXIF, 0) ==
          PHOTOC_ERR_METADATA);
    CHECK(photoc_error_kind_for_jpeg_edit(PHOTOC_JPEG_EDIT_UNSAFE_SOURCE, 0) ==
          PHOTOC_ERR_UNSUPPORTED);
    CHECK(photoc_error_kind_for_template(PHOTOC_TEMPLATE_MISSING_VALUE) ==
          PHOTOC_ERR_METADATA);
    CHECK(photoc_error_kind_for_template(PHOTOC_TEMPLATE_NO_MEMORY) ==
          PHOTOC_ERR_INTERNAL);

    int code = 0;
    char output[512];
    capture(report_collision, NULL, &code, output, sizeof(output));
    expect_line(PHOTOC_EXIT_FAILURE,
                "photoc compress: 'out.jpg': collision: output already exists\n",
                code, output);
    CHECK(strstr(output, "File exists") == NULL);

    capture(report_decode, NULL, &code, output, sizeof(output));
    expect_line(PHOTOC_EXIT_FAILURE,
                "photoc exif: 'bad.jpg': image decode error: invalid or truncated JPEG file\n",
                code, output);

    capture(report_warning, NULL, &code, output, sizeof(output));
    expect_line(0,
                "photoc duplicates: warning: 'locked.bin': file I/O error: unable to read file: Permission denied\n",
                code, output);

    capture(report_usage, NULL, &code, output, sizeof(output));
    expect_line(PHOTOC_EXIT_USAGE,
                "photoc sort: invalid input: invalid gap '30'\n",
                code, output);

    capture(report_skipped, NULL, &code, output, sizeof(output));
    expect_line(PHOTOC_EXIT_FAILURE,
                "photoc rename: 'a.jpg': skipped: collision: destination exists 'taken.jpg'\n",
                code, output);

    if (failures != 0) {
        fprintf(stdout, "%d error-model test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
