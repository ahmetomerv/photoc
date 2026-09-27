#ifndef PHOTOC_PARSE_H
#define PHOTOC_PARSE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool help;
    bool version;
    bool verbose;
    bool quiet;
    bool json;
    bool recursive;
    bool apply;
    const char *command;
    const char *first_argument;
    const char *format;
    size_t argument_count;
} photoc_cli_options;

typedef enum {
    PHOTOC_PARSE_OK,
    PHOTOC_PARSE_UNKNOWN_OPTION,
    PHOTOC_PARSE_MISSING_VALUE,
    PHOTOC_PARSE_DUPLICATE_OPTION
} photoc_parse_status;

/* command, first_argument, format, and error_arg borrow strings from argv. */
photoc_parse_status photoc_parse_args(int argc, char *argv[],
                                      photoc_cli_options *options,
                                      const char **error_arg);

#endif
