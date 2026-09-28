#include "photoc/output.h"

#include "photoc/error.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

void photoc_output_info(const photoc_output *output, const char *format, ...)
{
    if (output != NULL &&
        (output->level == PHOTOC_OUTPUT_QUIET || output->json)) {
        return;
    }
    int saved_errno = errno;
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
    errno = saved_errno;
}

void photoc_output_verbose(const photoc_output *output, const char *command,
                           const char *format, ...)
{
    if (output == NULL || output->level != PHOTOC_OUTPUT_VERBOSE) {
        return;
    }
    int saved_errno = errno;
    fprintf(stderr, "photoc %s: verbose: ", command);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    errno = saved_errno;
}

void photoc_output_metadata_warning(const photoc_output *output,
                                    const char *command, const char *path,
                                    photoc_metadata_result result,
                                    int system_errno)
{
    if (output != NULL && output->level == PHOTOC_OUTPUT_QUIET) {
        return;
    }
    int saved_errno = errno;
    photoc_error_metadata(command, PHOTOC_ERR_NOTE_WARNING, path, result,
                          system_errno);
    errno = saved_errno;
}
