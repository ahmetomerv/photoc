#include "photoc/parse.h"

#include <string.h>

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
        } else if (!options_ended && arg[0] == '-' && arg[1] != '\0') {
            *error_arg = arg;
            return PHOTOC_PARSE_UNKNOWN_OPTION;
        } else if (options->command == NULL) {
            options->command = arg;
        }
        /* Remaining positional arguments belong to the future command parser. */
    }

    return PHOTOC_PARSE_OK;
}
