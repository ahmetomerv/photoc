#include "photoc/parse.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool photoc_parse_gap_minutes(const char *text, uint32_t *minutes)
{
    if (text == NULL || minutes == NULL || text[0] < '0' || text[0] > '9') {
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

bool photoc_parse_size_bytes(const char *text, uint64_t *bytes)
{
    if (text == NULL || bytes == NULL || text[0] < '0' || text[0] > '9') {
        return false;
    }
    uint64_t value = 0;
    size_t i = 0;
    while (text[i] >= '0' && text[i] <= '9') {
        unsigned int digit = (unsigned int)(text[i] - '0');
        if (value > (UINT64_MAX - digit) / 10u) {
            return false;
        }
        value = value * 10u + digit;
        ++i;
    }
    uint64_t multiplier = 0;
    if (text[i] == '\0' || strcmp(text + i, "B") == 0) {
        multiplier = 1;
    } else if (strcmp(text + i, "KB") == 0) {
        multiplier = 1000;
    } else if (strcmp(text + i, "MB") == 0) {
        multiplier = 1000000;
    } else if (strcmp(text + i, "GB") == 0) {
        multiplier = UINT64_C(1000000000);
    } else if (strcmp(text + i, "KiB") == 0) {
        multiplier = 1024;
    } else if (strcmp(text + i, "MiB") == 0) {
        multiplier = UINT64_C(1048576);
    } else if (strcmp(text + i, "GiB") == 0) {
        multiplier = UINT64_C(1073741824);
    } else {
        return false;
    }
    if (value == 0 || value > UINT64_MAX / multiplier) {
        return false;
    }
    *bytes = value * multiplier;
    return true;
}

bool photoc_parse_nonnegative_double(const char *text, double *value)
{
    if (text == NULL || value == NULL || text[0] == '\0' || text[0] == '-') {
        return false;
    }
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if ((*cursor < '0' || *cursor > '9') && *cursor != '.' &&
            *cursor != 'e' && *cursor != 'E' && *cursor != '+' &&
            *cursor != '-') {
            return false;
        }
    }
    errno = 0;
    char *end = NULL;
    double parsed = strtod(text, &end);
    if (end == text || *end != '\0' || errno == ERANGE || !isfinite(parsed) ||
        parsed < 0.0) {
        return false;
    }
    *value = parsed;
    return true;
}

enum {
    CMD_COMPRESS = 1u << 0,
    CMD_EXIF = 1u << 1,
    CMD_DUPLICATES = 1u << 2,
    CMD_STATS = 1u << 3,
    CMD_RENAME = 1u << 4,
    CMD_SORT = 1u << 5,
    CMD_FOCUS = 1u << 6,
    CMD_SCRUB = 1u << 7,
    CMD_CHECK = 1u << 8,
    CMD_QUERY = 1u << 9,
    CMD_TIMELINE = 1u << 10,
    CMD_CONTACT = 1u << 11,
    CMD_RECURSIVE = CMD_COMPRESS | CMD_DUPLICATES | CMD_STATS | CMD_RENAME |
                    CMD_SORT | CMD_FOCUS | CMD_SCRUB | CMD_CHECK | CMD_QUERY |
                    CMD_TIMELINE | CMD_CONTACT,
    CMD_APPLY = CMD_RENAME | CMD_SORT
};

typedef struct {
    const char *name;
    unsigned commands; /* Zero accepts the flag for every command. */
    bool valued;
} photoc_flag;

static const photoc_flag flags[] = {
    {"-h", 0, false},
    {"--help", 0, false},
    {"-V", 0, false},
    {"--version", 0, false},
    {"-v", 0, false},
    {"--verbose", 0, false},
    {"-q", 0, false},
    {"--quiet", 0, false},
    {"--json", 0, false},
    {"--recursive", CMD_RECURSIVE, false},
    {"--apply", CMD_APPLY, false},
    {"--gps", CMD_SCRUB, false},
    {"--privacy", CMD_SCRUB, false},
    {"--all-metadata", CMD_SCRUB, false},
    {"--in-place", CMD_SCRUB, false},
    {"--quality", CMD_COMPRESS | CMD_CONTACT, true},
    {"--output", CMD_CONTACT, true},
    {"--columns", CMD_CONTACT, true},
    {"--thumb-size", CMD_CONTACT, true},
    {"--metadata", CMD_CONTACT, false},
    {"--sort", CMD_CONTACT, true},
    {"--target", CMD_COMPRESS, true},
    {"--min-quality", CMD_COMPRESS, true},
    {"--output-dir", CMD_COMPRESS, true},
    {"--by", CMD_SORT, true},
    {"--gap", CMD_SORT | CMD_TIMELINE, true},
    {"--format", CMD_RENAME, true},
    {"--threshold", CMD_FOCUS, true},
    {"--only-blurry", CMD_FOCUS, false},
    {"--only-errors", CMD_CHECK, false},
    {"--camera", CMD_QUERY, true},
    {"--make", CMD_QUERY, true},
    {"--iso", CMD_QUERY, true},
    {"--aperture", CMD_QUERY, true},
    {"--focal", CMD_QUERY, true},
    {"--after", CMD_QUERY, true},
    {"--before", CMD_QUERY, true},
    {"--has-gps", CMD_QUERY, false},
    {"--no-gps", CMD_QUERY, false},
    {"--print0", CMD_QUERY, false}};

static const photoc_flag *find_flag(const char *name)
{
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i) {
        if (strcmp(name, flags[i].name) == 0) {
            return &flags[i];
        }
    }
    return NULL;
}

static unsigned command_mask(const char *name)
{
    if (name == NULL) {
        return 0;
    }
    if (strcmp(name, "compress") == 0) {
        return CMD_COMPRESS;
    }
    if (strcmp(name, "exif") == 0) {
        return CMD_EXIF;
    }
    if (strcmp(name, "duplicates") == 0) {
        return CMD_DUPLICATES;
    }
    if (strcmp(name, "stats") == 0) {
        return CMD_STATS;
    }
    if (strcmp(name, "timeline") == 0) {
        return CMD_TIMELINE;
    }
    if (strcmp(name, "rename") == 0) {
        return CMD_RENAME;
    }
    if (strcmp(name, "sort") == 0) {
        return CMD_SORT;
    }
    if (strcmp(name, "focus") == 0) {
        return CMD_FOCUS;
    }
    if (strcmp(name, "scrub") == 0) {
        return CMD_SCRUB;
    }
    if (strcmp(name, "check") == 0) {
        return CMD_CHECK;
    }
    if (strcmp(name, "query") == 0) {
        return CMD_QUERY;
    }
    if (strcmp(name, "contact") == 0) {
        return CMD_CONTACT;
    }
    return 0;
}

static bool flag_allowed(const photoc_flag *flag, unsigned command)
{
    return flag->commands == 0 ||
           (command != 0 && (flag->commands & command) != 0);
}

/* The first positional token is the command. Known options, including those
   that belong to that command, may appear before it. */
static const char *prescan_command(int argc, char *argv[])
{
    bool options_ended = false;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!options_ended && strcmp(arg, "--") == 0) {
            options_ended = true;
            continue;
        }
        const photoc_flag *flag = options_ended ? NULL : find_flag(arg);
        if (flag != NULL) {
            if (flag->valued && i + 1 < argc && argv[i + 1][0] != '-') {
                ++i;
            }
            continue;
        }
        if (!options_ended && arg[0] == '-' && arg[1] != '\0') {
            continue;
        }
        return arg;
    }
    return NULL;
}

static photoc_parse_status take_value(int *index, int argc, char *argv[],
                                      const char **slot, const char *flag,
                                      const char **error_arg)
{
    if (*slot != NULL) {
        *error_arg = flag;
        return PHOTOC_PARSE_DUPLICATE_OPTION;
    }
    if (*index + 1 >= argc || argv[*index + 1][0] == '-') {
        *error_arg = flag;
        return PHOTOC_PARSE_MISSING_VALUE;
    }
    *slot = argv[++*index];
    return PHOTOC_PARSE_OK;
}

static photoc_parse_status apply_flag(const photoc_flag *flag, int *index,
                                      int argc, char *argv[],
                                      photoc_cli_options *options,
                                      const char **error_arg)
{
    const char *name = flag->name;
    if (!flag->valued) {
        if (strcmp(name, "-h") == 0 || strcmp(name, "--help") == 0) {
            options->help = true;
        } else if (strcmp(name, "-V") == 0 || strcmp(name, "--version") == 0) {
            options->version = true;
        } else if (strcmp(name, "-v") == 0 || strcmp(name, "--verbose") == 0) {
            options->verbose = true;
        } else if (strcmp(name, "-q") == 0 || strcmp(name, "--quiet") == 0) {
            options->quiet = true;
        } else if (strcmp(name, "--json") == 0) {
            options->json = true;
        } else if (strcmp(name, "--recursive") == 0) {
            options->recursive = true;
        } else if (strcmp(name, "--apply") == 0) {
            options->apply = true;
        } else if (strcmp(name, "--gps") == 0) {
            options->gps = true;
        } else if (strcmp(name, "--privacy") == 0) {
            options->privacy = true;
        } else if (strcmp(name, "--all-metadata") == 0) {
            options->all_metadata = true;
        } else if (strcmp(name, "--in-place") == 0) {
            options->in_place = true;
        } else if (strcmp(name, "--only-blurry") == 0) {
            options->only_blurry = true;
        } else if (strcmp(name, "--only-errors") == 0) {
            options->only_errors = true;
        } else if (strcmp(name, "--print0") == 0) {
            options->print0 = true;
        } else if (strcmp(name, "--has-gps") == 0) {
            options->has_gps = true;
        } else if (strcmp(name, "--no-gps") == 0) {
            options->no_gps = true;
        } else if (strcmp(name, "--metadata") == 0) {
            options->metadata = true;
        }
        return PHOTOC_PARSE_OK;
    }

    const char **slot = NULL;
    if (strcmp(name, "--quality") == 0) {
        slot = &options->quality;
    } else if (strcmp(name, "--output") == 0) {
        slot = &options->output_path;
    } else if (strcmp(name, "--columns") == 0) {
        slot = &options->columns;
    } else if (strcmp(name, "--thumb-size") == 0) {
        slot = &options->thumb_size;
    } else if (strcmp(name, "--sort") == 0) {
        slot = &options->contact_sort;
    } else if (strcmp(name, "--target") == 0) {
        slot = &options->target;
    } else if (strcmp(name, "--min-quality") == 0) {
        slot = &options->min_quality;
    } else if (strcmp(name, "--output-dir") == 0) {
        slot = &options->output_dir;
    } else if (strcmp(name, "--by") == 0) {
        slot = &options->sort_by;
    } else if (strcmp(name, "--gap") == 0) {
        slot = &options->gap;
    } else if (strcmp(name, "--format") == 0) {
        slot = &options->format;
    } else if (strcmp(name, "--threshold") == 0) {
        slot = &options->threshold;
    } else if (strcmp(name, "--camera") == 0) {
        slot = &options->camera;
    } else if (strcmp(name, "--make") == 0) {
        slot = &options->make;
    } else if (strcmp(name, "--iso") == 0) {
        slot = &options->iso;
    } else if (strcmp(name, "--aperture") == 0) {
        slot = &options->aperture;
    } else if (strcmp(name, "--focal") == 0) {
        slot = &options->focal;
    } else if (strcmp(name, "--after") == 0) {
        slot = &options->after;
    } else if (strcmp(name, "--before") == 0) {
        slot = &options->before;
    }
    if (slot == NULL) {
        *error_arg = name;
        return PHOTOC_PARSE_UNKNOWN_OPTION;
    }
    return take_value(index, argc, argv, slot, name, error_arg);
}

photoc_parse_status photoc_parse_args(int argc, char *argv[],
                                      photoc_cli_options *options,
                                      const char **error_arg)
{
    *options = (photoc_cli_options){0};
    *error_arg = NULL;
    unsigned command = command_mask(prescan_command(argc, argv));
    bool options_ended = false;

    for (int i = 1; i < argc; ++i) {
        const char *const arg = argv[i];
        if (!options_ended && strcmp(arg, "--") == 0) {
            options_ended = true;
            continue;
        }

        const photoc_flag *flag = options_ended ? NULL : find_flag(arg);
        if (flag != NULL && flag_allowed(flag, command)) {
            photoc_parse_status status =
                apply_flag(flag, &i, argc, argv, options, error_arg);
            if (status != PHOTOC_PARSE_OK) {
                return status;
            }
            continue;
        }
        if (!options_ended && arg[0] == '-' && arg[1] != '\0') {
            *error_arg = arg;
            return PHOTOC_PARSE_UNKNOWN_OPTION;
        }
        if (options->command == NULL) {
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
