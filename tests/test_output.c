#define _POSIX_C_SOURCE 200809L

#include "photoc/error.h"
#include "photoc/output.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Capture both streams to verify policy without changing any caller context.
   All FILEs/descriptors are owned by this function and released before return. */
static int check_output(const photoc_output *output, const char *expected_out,
                        const char *expected_err)
{
    FILE *out = tmpfile();
    FILE *err = tmpfile();
    int saved_out = dup(STDOUT_FILENO);
    int saved_err = dup(STDERR_FILENO);
    int failed = 0;
    if (out == NULL || err == NULL || saved_out < 0 || saved_err < 0) {
        failed = 1;
        goto cleanup;
    }
    fflush(stdout);
    fflush(stderr);
    if (dup2(fileno(out), STDOUT_FILENO) < 0 ||
        dup2(fileno(err), STDERR_FILENO) < 0) {
        failed = 1;
        goto cleanup;
    }
    errno = EBUSY;
    photoc_output_info(output, "status %d\n", 7);
    failed |= errno != EBUSY;
    photoc_output_verbose(output, "test", "count %d\n", 4);
    failed |= errno != EBUSY;
    photoc_output_metadata_warning(output, "test", "bad.jpg",
                                   PHOTOC_METADATA_INVALID_JPEG, 0);
    failed |= errno != EBUSY;
    /* Failure explanations bypass verbosity, even if their label is warning. */
    photoc_error_report("test", PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO, NULL,
                        "failed", 0);
    fflush(stdout);
    fflush(stderr);
    if (fseek(out, 0, SEEK_SET) != 0 || fseek(err, 0, SEEK_SET) != 0) {
        failed = 1;
        goto cleanup;
    }
    char actual_out[512] = {0};
    char actual_err[512] = {0};
    size_t out_length = fread(actual_out, 1, sizeof(actual_out) - 1, out);
    size_t err_length = fread(actual_err, 1, sizeof(actual_err) - 1, err);
    failed |= out_length != strlen(expected_out) ||
              err_length != strlen(expected_err) ||
              strcmp(actual_out, expected_out) != 0 ||
              strcmp(actual_err, expected_err) != 0;
cleanup:
    if (saved_out >= 0) {
        failed |= dup2(saved_out, STDOUT_FILENO) < 0;
        close(saved_out);
    }
    if (saved_err >= 0) {
        failed |= dup2(saved_err, STDERR_FILENO) < 0;
        close(saved_err);
    }
    if (out != NULL) {
        fclose(out);
    }
    if (err != NULL) {
        fclose(err);
    }
    return failed;
}

int main(void)
{
    const char *warning =
        "photoc test: warning: 'bad.jpg': image decode error: "
        "invalid or truncated JPEG file\n";
    const char *failure = "photoc test: file I/O error: failed\n";
    char normal_error[512];
    char verbose_error[512];
    snprintf(normal_error, sizeof(normal_error), "%s%s", warning, failure);
    snprintf(verbose_error, sizeof(verbose_error),
             "photoc test: verbose: count 4\n%s%s", warning, failure);
    const photoc_output cases[] = {
        {.level = PHOTOC_OUTPUT_NORMAL, .json = false},
        {.level = PHOTOC_OUTPUT_QUIET, .json = false},
        {.level = PHOTOC_OUTPUT_VERBOSE, .json = false},
        {.level = PHOTOC_OUTPUT_NORMAL, .json = true},
        {.level = PHOTOC_OUTPUT_QUIET, .json = true},
        {.level = PHOTOC_OUTPUT_VERBOSE, .json = true}};
    int failed = check_output(NULL, "status 7\n", normal_error);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const photoc_output *output = &cases[i];
        const char *expected_out =
            output->level == PHOTOC_OUTPUT_QUIET || output->json ? ""
                                                                 : "status 7\n";
        const char *expected_err =
            output->level == PHOTOC_OUTPUT_QUIET     ? failure
            : output->level == PHOTOC_OUTPUT_VERBOSE ? verbose_error
                                                     : normal_error;
        failed |= check_output(output, expected_out, expected_err);
    }
    if (failed) {
        fputs("output policy or errno preservation failed\n", stderr);
    }
    return failed;
}
