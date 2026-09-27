#include "photoc/parse.h"

#include <stdint.h>
#include <string.h>

bool photoc_parse_gap_minutes(const char *text, uint32_t *minutes)
{
    if (text == NULL || minutes == NULL || text[0] < '0' ||
        text[0] > '9') {
        return false;
    }
    uint32_t value = 0;
    size_t i = 0;
    while (text[i] >= '0' && text[i] <= '9') {
        unsigned int digit = (unsigned int)(text[i] - '0');
        if (value > (UINT32_MAX - digit) / 10u) {
            return false;
        }
        value = value * 10u + digit;
        ++i;
    }
    if (text[i] == 'm' && text[i + 1] == '\0') {
        *minutes = value;
        return true;
    }
    if (text[i] == 'h' && text[i + 1] == '\0' && value <= UINT32_MAX / 60u) {
        *minutes = value * 60u;
        return true;
    }
    return false;
}

photoc_parse_status photoc_parse_args(int argc, char *argv[],
                                      photoc_cli_options *options,
                                      const char **error_arg)
{
    *options = (photoc_cli_options){0};
    *error_arg = NULL;
    bool options_ended = false;

    for (int i = 1; i < argc; ++i) {
        const char *const arg = argv[i];

        if (!options_ended && strcmp(arg, "--") == 0) {
            options_ended = true;
        } else if (!options_ended &&
                   (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0)) {
            options->help = true;
        } else if (!options_ended &&
                   (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0)) {
            options->version = true;
        } else if (!options_ended &&
                   (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0)) {
            options->verbose = true;
        } else if (!options_ended &&
                   (strcmp(arg, "-q") == 0 || strcmp(arg, "--quiet") == 0)) {
            options->quiet = true;
        } else if (!options_ended && strcmp(arg, "--json") == 0) {
            options->json = true;
        } else if (!options_ended && options->command != NULL &&
                   (strcmp(options->command, "stats") == 0 ||
                    strcmp(options->command, "rename") == 0 ||
                    strcmp(options->command, "sort") == 0) &&
                   strcmp(arg, "--recursive") == 0) {
            options->recursive = true;
        } else if (!options_ended && options->command != NULL &&
                   strcmp(options->command, "sort") == 0 &&
                   strcmp(arg, "--by") == 0) {
            if (options->sort_by != NULL) {
                *error_arg = arg;
                return PHOTOC_PARSE_DUPLICATE_OPTION;
            }
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                *error_arg = arg;
                return PHOTOC_PARSE_MISSING_VALUE;
            }
            options->sort_by = argv[++i];
        } else if (!options_ended && options->command != NULL &&
                   strcmp(options->command, "sort") == 0 &&
                   strcmp(arg, "--gap") == 0) {
            if (options->gap != NULL) {
                *error_arg = arg;
                return PHOTOC_PARSE_DUPLICATE_OPTION;
            }
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                *error_arg = arg;
                return PHOTOC_PARSE_MISSING_VALUE;
            }
            options->gap = argv[++i];
        } else if (!options_ended && options->command != NULL &&
                   strcmp(options->command, "rename") == 0 &&
                   strcmp(arg, "--apply") == 0) {
            options->apply = true;
        } else if (!options_ended && options->command != NULL &&
                   strcmp(options->command, "rename") == 0 &&
                   strcmp(arg, "--format") == 0) {
            if (options->format != NULL) {
                *error_arg = arg;
                return PHOTOC_PARSE_DUPLICATE_OPTION;
            }
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                *error_arg = arg;
                return PHOTOC_PARSE_MISSING_VALUE;
            }
            options->format = argv[++i];
        } else if (!options_ended && arg[0] == '-' && arg[1] != '\0') {
            *error_arg = arg;
            return PHOTOC_PARSE_UNKNOWN_OPTION;
        } else if (options->command == NULL) {
            options->command = arg;
        } else {
            if (options->argument_count == 0) {
                options->first_argument = arg;
            }
            ++options->argument_count;
        }
    }

    return PHOTOC_PARSE_OK;
}
