#include "photoc/cli.h"

#include "photoc/commands.h"
#include "photoc/exit_codes.h"
#include "photoc/parse.h"
#include "photoc/session.h"
#include "photoc/version.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *syntax;
    const char *description;
} photoc_help_option;

typedef struct photoc_command photoc_command;

typedef int (*photoc_command_run)(const photoc_cli_options *options,
                                  const photoc_output *output);

struct photoc_command {
    const char *name;
    const char *description;
    const char *arguments;
    const char *status;
    const char *const *examples;
    const photoc_help_option *options;
    const char *const *notes;
    bool implemented;
    bool json_supported;
    photoc_command_run run;
};

static const char *const compress_examples[] = {
    "photoc compress photo.jpg --target 2MB",
    "photoc compress photo.jpg",
    "photoc compress photo.jpg --quality 80",
    "photoc compress photo.jpg --target 2MB --min-quality 30",
    "photoc compress ~/Pictures --recursive --quality 75 --output-dir "
    "~/Compressed",
    NULL};

static const char *const contact_examples[] = {
    "photoc contact ./photos --output sheet.jpg",
    "photoc contact ./photos --output sheet.jpg --metadata",
    "photoc contact ./photos --output sheet.jpg --recursive --sort date",
    NULL};

static const photoc_help_option contact_options[] = {
    {"--output <file>", "Required JPEG output path; pages use -001, -002, ..."},
    {"--recursive", "Include nested directories"},
    {"--columns <1-8>", "Columns per sheet (default: 4)"},
    {"--thumb-size <96-512>", "Thumbnail box in pixels (default: 240)"},
    {"--quality <1-100>", "JPEG quality (default: 85)"},
    {"--metadata", "Show available ISO, aperture, shutter and focal length"},
    {"--sort name|date", "Sort by filename (default) or capture timestamp"},
    {NULL, NULL}};

static const char *const contact_notes[] = {
    "Up to 24 tiles per page and at most 6 rows; pages are never overwritten.",
    "EXIF orientation is applied to thumbnails. Output has no source EXIF or ICC.",
    "Non-ASCII filename characters display as '?' in the built-in font.",
    NULL};

static const photoc_help_option compress_options[] = {
    {"--quality <1-100>", "JPEG quality (default: 80)"},
    {"--target <size>",
     "Maximum output size; searches quality 20-100 by default"},
    {"--min-quality <n>", "Minimum quality for --target (default: 20)"},
    {"--recursive", "Include nested directories"},
    {"--output-dir <dir>",
     "Put copies in this directory; preserve relative paths"},
    {NULL, NULL}};

static const char *const compress_notes[] = {
    "Output: photo.compressed.jpg; existing outputs are skipped in directory "
    "mode.",
    "Sizes use bytes, KB/MB/GB (decimal), or KiB/MiB/GiB (binary).",
    "An unreachable target writes at minimum quality and returns failure.",
    "EXIF, ICC, and standard/Extended XMP are preserved, including GPS.",
    "Invalid ICC chunk sets fail without publishing a copy.",
    NULL};

static const char *const exif_examples[] = {"photoc exif photo.jpg",
                                            "photoc exif photo.jpg --json",
                                            "photoc exif DSC00001.ARW", NULL};

static const photoc_help_option exif_options[] = {
    {"--json", "Print structured JSON"}, {NULL, NULL}};

static const char *const duplicates_examples[] = {
    "photoc duplicates ~/Pictures", "photoc duplicates ~/Pictures --recursive",
    "photoc duplicates ~/Pictures --json", NULL};

static const photoc_help_option scan_options[] = {
    {"--recursive", "Include nested directories"},
    {"--json", "Print structured JSON"},
    {NULL, NULL}};

static const char *const stats_examples[] = {
    "photoc stats ~/Pictures", "photoc stats ~/Pictures --recursive",
    "photoc stats ~/Pictures --json", NULL};

static const char *const timeline_examples[] = {
    "photoc timeline ~/Pictures/Prague",
    "photoc timeline ~/Pictures/Prague --recursive --gap 30m",
    "photoc timeline ~/Pictures/Prague --gap 2h --json", NULL};

static const photoc_help_option timeline_options[] = {
    {"--recursive", "Include nested directories"},
    {"--gap <duration>", "Session gap (for example 30m or 2h; default 60m)"},
    {"--json", "Print date/session hierarchy and scan summary"},
    {NULL, NULL}};

static const char *const timeline_notes[] = {
    "Group by recorded calendar date, then capture-time session.",
    "An exact gap stays in one session; midnight always starts another.",
    "Missing/invalid capture times are counted and excluded; no timezone is "
    "inferred.",
    NULL};

static const char *const rename_examples[] = {
    "photoc rename ~/Pictures --format \"{date}_{camera}_{sequence}.{ext}\"",
    "photoc rename ~/Pictures --format \"{original}_{sequence}.{ext}\" "
    "--recursive",
    "photoc rename ~/Pictures --format \"{date}_{camera}_{sequence}.{ext}\" "
    "--apply",
    NULL};

static const photoc_help_option rename_options[] = {
    {"--format <template>", "Required filename template"},
    {"--recursive", "Include nested directories"},
    {"--apply", "Rename files after the whole plan passes preflight"},
    {NULL, NULL}};

static const char *const sort_examples[] = {
    "photoc sort ~/Pictures --by date",
    "photoc sort ~/Pictures --by session --gap 30m",
    "photoc sort ~/Pictures --by session --gap 2h --recursive",
    "photoc sort ~/Pictures --by date --apply", NULL};

static const photoc_help_option sort_options[] = {
    {"--by date|session", "Required; group by date or capture-time session"},
    {"--gap <duration>", "Session gap (for example 30m or 2h; default 60m)"},
    {"--recursive", "Include nested directories"},
    {"--apply", "Move files after the whole plan passes preflight"},
    {NULL, NULL}};

static const char *const focus_examples[] = {
    "photoc focus photo.jpg", "photoc focus ~/Pictures --recursive",
    "photoc focus ~/Pictures --threshold 100 --only-blurry",
    "photoc focus photo.jpg --json", NULL};

static const photoc_help_option focus_options[] = {
    {"--recursive", "Include nested directories"},
    {"--threshold <value>", "Nonnegative review cutoff (default: 100)"},
    {"--only-blurry", "List only scores below the threshold"},
    {"--json", "Print structured JSON"},
    {NULL, NULL}};

static const char *const focus_notes[] = {
    "Rows are sorted by score ascending, then path; summaries include all "
    "analyzed JPEGs.",
    "Possibly blurry is a review hint, not certainty; no universal cutoff "
    "exists.",
    "Noise, texture, subject matter, and JPEG artifacts affect scores.", NULL};

static const char *const scrub_examples[] = {
    "photoc scrub photo.jpg --gps",
    "photoc scrub ~/Pictures --gps --recursive",
    "photoc scrub photo.jpg --privacy",
    "photoc scrub photo.jpg --all-metadata",
    "photoc scrub photo.jpg --gps --in-place",
    NULL};

static const char *const check_examples[] = {
    "photoc check photo.jpg", "photoc check ~/Pictures --recursive",
    "photoc check ~/Pictures --recursive --only-errors",
    "photoc check ~/Pictures --json", NULL};

static const photoc_help_option check_options[] = {
    {"--recursive", "Include nested directories"},
    {"--only-errors", "List ERROR rows only; summary counts all checked files"},
    {"--json", "Print structured JSON"},
    {NULL, NULL}};

static const char *const check_notes[] = {
    "Read-only; rows are sorted by path. Warnings return 0; errors return 1.",
    "Checks structural readability, not visual quality or complete metadata "
    "validity.",
    NULL};

static const photoc_help_option scrub_options[] = {
    {"--gps", "Remove EXIF GPS tags (original behavior)"},
    {"--privacy", "Remove supported location and identifier fields"},
    {"--all-metadata", "Remove descriptive metadata; keep ICC and orientation"},
    {"--recursive", "Include nested directories"},
    {"--in-place", "Replace each original after temporary-file verification"},
    {NULL, NULL}};

static const char *const query_examples[] = {
    "photoc query ~/Photos --iso \">800\"",
    "photoc query ~/Photos --camera \"DSC-RX100M7A\" --aperture \"<=4\"",
    "photoc query ~/Photos --after 2026-01-01 --before 2026-12-31",
    "photoc query ~/Photos --recursive --has-gps --print0",
    "photoc query ~/Photos --recursive --json",
    NULL};

static const photoc_help_option query_options[] = {
    {"--camera <value>", "Exact, case-sensitive camera model"},
    {"--make <value>", "Exact, case-sensitive camera make"},
    {"--iso <expression>", "Compare ISO: 100, =100, >100, >=100, <100, <=100"},
    {"--aperture <expr>", "Compare f-number with the same operators"},
    {"--focal <expr>", "Compare focal length in millimeters"},
    {"--after YYYY-MM-DD", "Capture date on or after this day (inclusive)"},
    {"--before YYYY-MM-DD", "Capture date on or before this day (inclusive)"},
    {"--has-gps", "Require valid GPS coordinates"},
    {"--no-gps", "Require no valid GPS coordinates"},
    {"--recursive", "Include nested directories"},
    {"--print0", "NUL-separated paths; exclusive with --json"},
    {"--json", "Print filters, matches, metadata, and scan summary"},
    {NULL, NULL}};

static const char *const query_notes[] = {
    "Read-only; all filters are ANDed. Missing queried fields do not match.",
    "Default stdout contains only sorted paths. Use --print0 for xargs -0.",
    "Dates use local EXIF capture time without timezone conversion.",
    "No matches returns 0; scan/load failures return 1; invalid usage returns "
    "2.",
    NULL};

static const char *const scrub_notes[] = {
    "Choose exactly one of --gps, --privacy, or --all-metadata.",
    "MakerNotes and unknown marker formats can retain private information.",
    "Warning: --in-place replaces original files and creates no backup.",
    "It preserves mode bits; ACLs and extended attributes may change.",
    "Linked, symlinked, changed, or differently owned files are refused.",
    NULL};

#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
static void command_error(const char *command, const char *format, ...)
{
    fprintf(stderr, "photoc %s: ", command);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

static void usage_error(const char *command, const char *message,
                        const char *usage)
{
    command_error(command, "%s\nUsage: photoc %s\n", message, usage);
}

static bool parse_quality_value(const char *value, int *quality)
{
    if (value == NULL || value[0] == '\0') {
        return false;
    }
    int parsed = 0;
    for (const char *digit = value; *digit != '\0'; ++digit) {
        if (*digit < '0' || *digit > '9' ||
            parsed > (100 - (*digit - '0')) / 10) {
            return false;
        }
        parsed = parsed * 10 + (*digit - '0');
    }
    if (parsed < 1 || parsed > 100) {
        return false;
    }
    *quality = parsed;
    return true;
}

static bool parse_bounded_uint(const char *value, uint32_t minimum,
                               uint32_t maximum, uint32_t *number)
{
    if (value == NULL || value[0] == '\0')
        return false;
    uint32_t parsed = 0;
    for (const char *digit = value; *digit != '\0'; ++digit) {
        if (*digit < '0' || *digit > '9')
            return false;
        uint32_t value_digit = (uint32_t)(*digit - '0');
        if (value_digit > maximum ||
            parsed > (maximum - value_digit) / 10u)
            return false;
        parsed = parsed * 10u + value_digit;
    }
    if (parsed < minimum || parsed > maximum)
        return false;
    *number = parsed;
    return true;
}

static int run_contact(const photoc_cli_options *options,
                       const photoc_output *output)
{
    static const char usage[] =
        "contact <directory> --output <file.jpg> [--recursive] "
        "[--columns <1-8>] [--thumb-size <96-512>] [--quality <1-100>] "
        "[--metadata] [--sort name|date]";
    if (options->argument_count != 1 || options->output_path == NULL) {
        usage_error("contact", "expected one directory and --output <file.jpg>",
                    usage);
        return PHOTOC_EXIT_USAGE;
    }
    photoc_contact_options contact = {.output_path = options->output_path,
                                      .columns = 4,
                                      .thumb_size = 240,
                                      .quality = 85,
                                      .recursive = options->recursive,
                                      .metadata = options->metadata};
    if (options->columns != NULL &&
        !parse_bounded_uint(options->columns, 1, 8, &contact.columns)) {
        command_error("contact", "invalid columns '%s'; use 1-8\n",
                      options->columns);
        return PHOTOC_EXIT_USAGE;
    }
    if (options->thumb_size != NULL &&
        !parse_bounded_uint(options->thumb_size, 96, 512,
                            &contact.thumb_size)) {
        command_error("contact", "invalid thumb size '%s'; use 96-512\n",
                      options->thumb_size);
        return PHOTOC_EXIT_USAGE;
    }
    if (options->quality != NULL &&
        !parse_quality_value(options->quality, &contact.quality)) {
        command_error("contact", "invalid quality '%s'; use 1-100\n",
                      options->quality);
        return PHOTOC_EXIT_USAGE;
    }
    if (options->contact_sort != NULL) {
        if (strcmp(options->contact_sort, "date") == 0)
            contact.sort_date = true;
        else if (strcmp(options->contact_sort, "name") != 0) {
            command_error("contact", "invalid sort '%s'; use name or date\n",
                          options->contact_sort);
            return PHOTOC_EXIT_USAGE;
        }
    }
    return photoc_command_contact_with_output(options->first_argument,
                                              &contact, output);
}

static int run_compress(const photoc_cli_options *options,
                        const photoc_output *output)
{
    static const char usage[] =
        "compress <file|directory> [--quality <1-100> | --target <size> "
        "[--min-quality <1-100>]] [--recursive] [--output-dir <directory>]";
    if (options->argument_count != 1) {
        usage_error("compress", "expected exactly one JPEG file or directory",
                    usage);
        return PHOTOC_EXIT_USAGE;
    }
    if (options->quality != NULL && options->target != NULL) {
        command_error("compress",
                      "--quality and --target cannot be combined\n");
        return PHOTOC_EXIT_USAGE;
    }
    if (options->min_quality != NULL && options->target == NULL) {
        command_error("compress", "--min-quality requires --target\n");
        return PHOTOC_EXIT_USAGE;
    }
    photoc_compress_options compress = {.quality = 80,
                                        .min_quality = 20,
                                        .recursive = options->recursive,
                                        .output_dir = options->output_dir};
    if (options->quality != NULL &&
        !parse_quality_value(options->quality, &compress.quality)) {
        command_error("compress", "invalid quality '%s'; use 1-100\n",
                      options->quality);
        return PHOTOC_EXIT_USAGE;
    }
    if (options->min_quality != NULL &&
        !parse_quality_value(options->min_quality, &compress.min_quality)) {
        command_error("compress", "invalid minimum quality '%s'; use 1-100\n",
                      options->min_quality);
        return PHOTOC_EXIT_USAGE;
    }
    if (options->target != NULL &&
        !photoc_parse_size_bytes(options->target, &compress.target_bytes)) {
        command_error("compress",
                      "invalid target '%s'; use positive bytes or "
                      "KB/MB/GB/KiB/MiB/GiB\n",
                      options->target);
        return PHOTOC_EXIT_USAGE;
    }
    return photoc_command_compress_with_output(options->first_argument,
                                               &compress, output);
}

static int run_exif(const photoc_cli_options *options,
                    const photoc_output *output)
{
    if (options->argument_count != 1) {
        usage_error("exif", "expected exactly one file", "exif <file>");
        return PHOTOC_EXIT_USAGE;
    }
    return photoc_command_exif_with_output(options->first_argument,
                                           options->json, output);
}

static int run_duplicates(const photoc_cli_options *options,
                          const photoc_output *output)
{
    static const char usage[] = "duplicates <directory> [--recursive] [--json]";
    if (options->argument_count != 1) {
        usage_error("duplicates", "expected exactly one directory", usage);
        return PHOTOC_EXIT_USAGE;
    }
    return photoc_command_duplicates_with_output(
        options->first_argument, options->recursive, options->json, output);
}

static int run_stats(const photoc_cli_options *options,
                     const photoc_output *output)
{
    static const char usage[] = "stats <directory> [--recursive] [--json]";
    if (options->argument_count != 1) {
        usage_error("stats", "expected exactly one directory", usage);
        return PHOTOC_EXIT_USAGE;
    }
    return photoc_command_stats_with_output(
        options->first_argument, options->recursive, options->json, output);
}

static int run_timeline(const photoc_cli_options *options,
                        const photoc_output *output)
{
    static const char usage[] =
        "timeline <directory> [--recursive] [--gap <duration>] [--json]";
    if (options->argument_count != 1) {
        usage_error("timeline", "expected exactly one directory", usage);
        return PHOTOC_EXIT_USAGE;
    }
    uint32_t gap_minutes = PHOTOC_SESSION_DEFAULT_GAP_MINUTES;
    if (options->gap != NULL &&
        !photoc_parse_gap_minutes(options->gap, &gap_minutes)) {
        command_error(
            "timeline",
            "invalid gap '%s'; use minutes or hours such as 30m or 2h\n",
            options->gap);
        return PHOTOC_EXIT_USAGE;
    }
    return photoc_command_timeline_with_output(options->first_argument,
                                               options->recursive, gap_minutes,
                                               options->json, output);
}

static int run_rename(const photoc_cli_options *options,
                      const photoc_output *output)
{
    static const char usage[] =
        "rename <directory> --format <template> [--recursive] [--apply]";
    if (options->argument_count != 1 || options->format == NULL) {
        usage_error("rename", "expected one directory and --format <template>",
                    usage);
        return PHOTOC_EXIT_USAGE;
    }
    return photoc_command_rename_with_output(
        options->first_argument, options->format, options->recursive,
        options->apply, output);
}

static int run_sort(const photoc_cli_options *options,
                    const photoc_output *output)
{
    static const char usage[] =
        "sort <directory> --by date|session [--gap <duration>] [--recursive] "
        "[--apply]";
    if (options->argument_count != 1 || options->sort_by == NULL) {
        usage_error("sort", "expected one directory and --by date|session",
                    usage);
        return PHOTOC_EXIT_USAGE;
    }
    photoc_sort_mode mode;
    if (strcmp(options->sort_by, "date") == 0) {
        mode = PHOTOC_SORT_BY_DATE;
    } else if (strcmp(options->sort_by, "session") == 0) {
        mode = PHOTOC_SORT_BY_SESSION;
    } else {
        command_error("sort",
                      "unsupported sort key '%s'; use 'date' or 'session'\n",
                      options->sort_by);
        return PHOTOC_EXIT_USAGE;
    }
    if (mode == PHOTOC_SORT_BY_DATE && options->gap != NULL) {
        command_error("sort", "--gap is only valid with --by session\n");
        return PHOTOC_EXIT_USAGE;
    }
    uint32_t gap_minutes = PHOTOC_SESSION_DEFAULT_GAP_MINUTES;
    if (options->gap != NULL &&
        !photoc_parse_gap_minutes(options->gap, &gap_minutes)) {
        command_error(
            "sort",
            "invalid gap '%s'; use minutes or hours such as 30m or 2h\n",
            options->gap);
        return PHOTOC_EXIT_USAGE;
    }
    return photoc_command_sort_with_output(options->first_argument,
                                           options->recursive, mode,
                                           gap_minutes, options->apply, output);
}

static int run_focus(const photoc_cli_options *options,
                     const photoc_output *output)
{
    static const char usage[] =
        "focus <file|directory> [--recursive] [--threshold <value>] "
        "[--only-blurry] [--json]";
    if (options->argument_count != 1) {
        usage_error("focus", "expected exactly one JPEG file or directory",
                    usage);
        return PHOTOC_EXIT_USAGE;
    }
    double threshold = PHOTOC_FOCUS_DEFAULT_THRESHOLD;
    if (options->threshold != NULL &&
        !photoc_parse_nonnegative_double(options->threshold, &threshold)) {
        command_error("focus",
                      "invalid threshold '%s'; use a finite nonnegative "
                      "number\n",
                      options->threshold);
        return PHOTOC_EXIT_USAGE;
    }
    return photoc_command_focus_with_output(
        options->first_argument, options->recursive, threshold,
        options->only_blurry, options->json, output);
}

static int run_scrub(const photoc_cli_options *options,
                     const photoc_output *output)
{
    static const char usage[] =
        "scrub <file|directory> (--gps|--privacy|--all-metadata) [--recursive] "
        "[--in-place]";
    unsigned int modes = (unsigned int)options->gps +
                         (unsigned int)options->privacy +
                         (unsigned int)options->all_metadata;
    if (options->argument_count != 1 || modes != 1) {
        usage_error("scrub", "expected one path and exactly one scrub mode",
                    usage);
        return PHOTOC_EXIT_USAGE;
    }
    if (options->privacy || options->all_metadata) {
        return photoc_command_scrub_mode_with_output(
            options->first_argument, options->recursive, options->in_place,
            options->privacy ? PHOTOC_SCRUB_PRIVACY : PHOTOC_SCRUB_ALL_METADATA,
            output);
    }
    return photoc_command_scrub_with_output(
        options->first_argument, options->recursive, options->in_place, output);
}

static int run_check(const photoc_cli_options *options,
                     const photoc_output *output)
{
    static const char usage[] =
        "check <file|directory> [--recursive] [--only-errors] [--json]";
    if (options->argument_count != 1) {
        usage_error("check", "expected exactly one JPEG file or directory",
                    usage);
        return PHOTOC_EXIT_USAGE;
    }
    return photoc_command_check_with_output(
        options->first_argument, options->recursive, options->only_errors,
        options->json, output);
}

static int run_query(const photoc_cli_options *options,
                     const photoc_output *output)
{
    if (options->argument_count != 1) {
        usage_error("query", "expected exactly one JPEG file or directory",
                    "query <file|directory> [filters] [--recursive] "
                    "[--json | --print0]");
        return PHOTOC_EXIT_USAGE;
    }
    if (options->json && options->print0) {
        command_error("query", "--json and --print0 are mutually exclusive\n");
        return PHOTOC_EXIT_USAGE;
    }
    photoc_query_filters filters = {.camera = options->camera,
                                    .make = options->make,
                                    .iso = options->iso,
                                    .aperture = options->aperture,
                                    .focal = options->focal,
                                    .after = options->after,
                                    .before = options->before,
                                    .has_gps = options->has_gps,
                                    .no_gps = options->no_gps};
    photoc_query query;
    const char *invalid = NULL;
    if (!photoc_query_init(&filters, &query, &invalid)) {
        command_error("query",
                      "invalid or conflicting filter %s; "
                      "see 'photoc query --help'\n",
                      invalid);
        return PHOTOC_EXIT_USAGE;
    }
    return photoc_command_query_with_output(options->first_argument, &query,
                                            options->recursive, options->json,
                                            options->print0, output);
}

static const photoc_command commands[] = {
    {"query", "Search JPEG metadata and print matching paths",
     "<file|directory> [filters] [--recursive] [--json | --print0]",
     "Status: available for JPEG files (read-only)", query_examples,
     query_options, query_notes, true, true, run_query},
    {"check", "Audit JPEG structural readability",
     "<file|directory> [--recursive] [--only-errors] [--json]",
     "Status: available for JPEG files (read-only)", check_examples,
     check_options, check_notes, true, true, run_check},
    {"compress", "Re-encode JPEGs by quality or target size",
     "<file|directory> [--quality <1-100> | --target <size> [--min-quality "
     "<1-100>]] [--recursive] [--output-dir <directory>]",
     "Status: available for JPEG files; writes .compressed copies",
     compress_examples, compress_options, compress_notes, true, false,
     run_compress},
    {"contact", "Generate paged JPEG contact sheets",
     "<directory> --output <file.jpg> [--recursive] [--columns <1-8>] "
     "[--thumb-size <96-512>] [--quality <1-100>] [--metadata] "
     "[--sort name|date]",
     "Status: writes new JPEG sheet(s); never overwrites",
     contact_examples, contact_options, contact_notes, true, false,
     run_contact},
    {"exif", "Inspect JPEG and Sony ARW metadata", "<file>",
     "Status: available for JPEG and Sony ARW metadata", exif_examples,
     exif_options, NULL, true, true, run_exif},
    {"duplicates", "Find exact duplicate files",
     "<directory> [--recursive] [--json]",
     "Status: available for regular files (read-only)", duplicates_examples,
     scan_options, NULL, true, true, run_duplicates},
    {"stats", "Summarize JPEG and Sony ARW collections",
     "<directory> [--recursive] [--json]",
     "Status: available for JPEG and Sony ARW metadata", stats_examples,
     scan_options, NULL, true, true, run_stats},
    {"timeline", "Summarize shooting dates and sessions",
     "<directory> [--recursive] [--gap <duration>] [--json]",
     "Status: read-only JPEG and Sony ARW metadata", timeline_examples,
     timeline_options, timeline_notes, true, true, run_timeline},
    {"rename", "Preview or apply photo renames",
     "<directory> --format <template> [--recursive] [--apply]",
     "Status: dry-run by default; --apply changes files after preflight",
     rename_examples, rename_options, NULL, true, false, run_rename},
    {"sort", "Preview or apply photo sorting",
     "<directory> --by date|session [--gap <duration>] [--recursive] [--apply]",
     "Status: dry-run by default; --apply moves files after preflight",
     sort_examples, sort_options, NULL, true, false, run_sort},
    {"focus", "Compare JPEG sharpness scores",
     "<file|directory> [--recursive] [--threshold <value>] [--only-blurry] "
     "[--json]",
     "Status: available for JPEG files (read-only)", focus_examples,
     focus_options, focus_notes, true, true, run_focus},
    {"scrub", "Remove GPS or descriptive metadata from JPEGs",
     "<file|directory> (--gps|--privacy|--all-metadata) [--recursive] "
     "[--in-place]",
     "Status: writes .scrubbed copies by default; --in-place replaces "
     "originals",
     scrub_examples, scrub_options, scrub_notes, true, false, run_scrub}};

static const photoc_command *find_command(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (strcmp(name, commands[i].name) == 0) {
            return &commands[i];
        }
    }
    return NULL;
}

static void print_global_help(void)
{
    puts("photoc - Command-line tools for photographers.");
    puts("");
    puts("Usage: photoc [global options] <command> [args]");
    puts("       photoc <command> --help");
    puts("");
    puts("Commands:");
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        printf("  %-12s %s\n", commands[i].name, commands[i].description);
    }
    puts("");
    puts("Global options:");
    puts("  -h, --help       Show global or command help");
    puts("  -V, --version    Show the version");
    puts("  -v, --verbose    Add diagnostics on stderr");
    puts("  -q, --quiet      Suppress status text and non-critical warnings");
    puts("      --json       Request JSON output where supported");
    puts("");
    puts("Exif and stats inspect JPEG/ARW metadata; duplicates compares file "
         "bytes.");
    puts(
        "Rename and sort preview by default; compress, scrub, and contact write JPEGs.");
    puts("Focus scores JPEG sharpness as a review aid, not a blur verdict.");
    puts("Run 'photoc <command> --help' for usage and examples.");
}

static void print_option(const photoc_help_option *option)
{
    printf("  %-20s %s\n", option->syntax, option->description);
}

static void print_command_help(const photoc_command *command)
{
    printf("Usage: photoc [global options] %s %s\n", command->name,
           command->arguments);
    printf("       photoc %s --help\n\n", command->name);
    puts(command->description);
    puts("");
    puts(command->status);
    puts("");
    puts(command->implemented ? "Example:" : "Example (planned):");
    for (size_t i = 0; command->examples[i] != NULL; ++i) {
        printf("  %s\n", command->examples[i]);
    }
    if (command->options != NULL) {
        puts("");
        puts("Options:");
        for (size_t i = 0; command->options[i].syntax != NULL; ++i) {
            print_option(&command->options[i]);
        }
    }
    if (command->notes != NULL) {
        puts("");
        for (size_t i = 0; command->notes[i] != NULL; ++i) {
            puts(command->notes[i]);
        }
    }
}

static void report_parse_error(photoc_parse_status status, const char *command,
                               const char *error_arg)
{
    if (command == NULL) {
        if (status == PHOTOC_PARSE_UNKNOWN_OPTION) {
            fprintf(stderr,
                    "photoc: unknown option '%s'\n"
                    "Try 'photoc --help' for available options.\n",
                    error_arg);
        } else if (status == PHOTOC_PARSE_MISSING_VALUE) {
            fprintf(stderr, "photoc: option '%s' requires a value\n",
                    error_arg);
        } else {
            fprintf(stderr, "photoc: option '%s' may be specified only once\n",
                    error_arg);
        }
        return;
    }

    if (status == PHOTOC_PARSE_UNKNOWN_OPTION) {
        command_error(command,
                      "unknown option '%s'\n"
                      "Try 'photoc %s --help' for available options.\n",
                      error_arg, command);
    } else if (status == PHOTOC_PARSE_MISSING_VALUE) {
        command_error(command, "option '%s' requires a value\n", error_arg);
    } else {
        command_error(command, "option '%s' may be specified only once\n",
                      error_arg);
    }
}

int photoc_run(int argc, char *argv[])
{
    photoc_cli_options options;
    const char *error_arg;
    photoc_parse_status status =
        photoc_parse_args(argc, argv, &options, &error_arg);
    const photoc_command *command = find_command(options.command);

    if (status != PHOTOC_PARSE_OK) {
        report_parse_error(status, command == NULL ? NULL : command->name,
                           error_arg);
        return PHOTOC_EXIT_USAGE;
    }

    if (options.help && options.version) {
        fputs("photoc: --help and --version cannot be used together\n", stderr);
        return PHOTOC_EXIT_USAGE;
    }

    if (options.verbose && options.quiet) {
        fputs("photoc: --verbose and --quiet cannot be used together\n",
              stderr);
        return PHOTOC_EXIT_USAGE;
    }

    if (options.command != NULL && command == NULL) {
        fprintf(stderr,
                "photoc: unknown command '%s'\n"
                "Try 'photoc --help' for available commands.\n",
                options.command);
        return PHOTOC_EXIT_USAGE;
    }

    if (options.version && command != NULL) {
        fputs("photoc: --version cannot be used with a command\n", stderr);
        return PHOTOC_EXIT_USAGE;
    }

    if (options.help) {
        if (command == NULL) {
            print_global_help();
        } else {
            print_command_help(command);
        }
        return PHOTOC_EXIT_SUCCESS;
    }

    if (options.version) {
        puts("photoc " PHOTOC_VERSION);
        return PHOTOC_EXIT_SUCCESS;
    }

    if (command == NULL) {
        fputs("photoc: missing command\n"
              "Usage: photoc [global options] <command> [args]\n"
              "Try 'photoc --help' for available commands.\n",
              stderr);
        return PHOTOC_EXIT_USAGE;
    }

    if (!command->implemented) {
        return photoc_command_unimplemented(command->name);
    }
    if (!command->json_supported && options.json) {
        command_error(command->name, "--json is not supported\n");
        return PHOTOC_EXIT_USAGE;
    }
    const photoc_output output = {.level = options.quiet ? PHOTOC_OUTPUT_QUIET
                                           : options.verbose
                                               ? PHOTOC_OUTPUT_VERBOSE
                                               : PHOTOC_OUTPUT_NORMAL,
                                  .json = options.json};
    return command->run(&options, &output);
}
