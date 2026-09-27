#include "photoc/cli.h"

#include "photoc/commands.h"
#include "photoc/exit_codes.h"
#include "photoc/parse.h"
#include "photoc/version.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    const char *description;
    const char *arguments;
    const char *example;
} photoc_command;

static const photoc_command commands[] = {
    {"compress", "Compress image files", "<photo>...", "photoc compress photo.jpg"},
    {"exif", "Inspect JPEG metadata", "<file>", "photoc exif photo.jpg"},
    {"duplicates", "Find duplicate photos", "<path>...", "photoc duplicates ~/Pictures"},
    {"stats", "Summarize JPEG collections", "<directory> [--recursive] [--json]", "photoc stats ~/Pictures"},
    {"rename", "Rename photo files", "<photo>...", "photoc rename photo.jpg"},
    {"sort", "Organize photos into folders", "<path>...", "photoc sort ~/Pictures"},
    {"focus", "Assess image focus", "<photo>...", "photoc focus photo.jpg"},
    {"scrub", "Remove selected metadata", "<photo>...", "photoc scrub photo.jpg"}
};

static const photoc_command *find_command(const char *name)
{
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (strcmp(name, commands[i].name) == 0) {
            return &commands[i];
        }
    }
    return NULL;
}

static void print_global_help(void)
{
    puts("photoc - command-line toolkit for photographers");
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
    puts("  -v, --verbose    Request detailed output");
    puts("  -q, --quiet      Request reduced output");
    puts("      --json       Request JSON output where supported");
    puts("");
    puts("The exif and stats commands support JPEG files; other commands are planned.");
    puts("Run 'photoc <command> --help' for usage and examples.");
}

static void print_command_help(const photoc_command *command)
{
    printf("Usage: photoc [global options] %s %s\n",
           command->name, command->arguments);
    printf("       photoc %s --help\n\n", command->name);
    puts(command->description);
    puts("");
    if (strcmp(command->name, "exif") == 0 ||
        strcmp(command->name, "stats") == 0) {
        puts("Status: available for JPEG files");
    } else {
        puts("Status: not implemented");
    }
    puts("");
    bool implemented = strcmp(command->name, "exif") == 0 ||
                       strcmp(command->name, "stats") == 0;
    puts(implemented ? "Example:" : "Example (planned):");
    printf("  %s\n", command->example);
    if (strcmp(command->name, "exif") == 0) {
        puts("  photoc exif photo.jpg --json");
    } else if (strcmp(command->name, "stats") == 0) {
        puts("  photoc stats ~/Pictures --recursive");
        puts("  photoc stats ~/Pictures --json");
    }
}

int photoc_run(int argc, char *argv[])
{
    photoc_cli_options options;
    const char *error_arg;
    photoc_parse_status status = photoc_parse_args(argc, argv, &options, &error_arg);

    if (status == PHOTOC_PARSE_UNKNOWN_OPTION) {
        fprintf(stderr, "photoc: unknown option '%s'\n"
                        "Try 'photoc --help' for available options.\n", error_arg);
        return PHOTOC_EXIT_USAGE;
    }

    if (options.help && options.version) {
        fputs("photoc: --help and --version cannot be used together\n", stderr);
        return PHOTOC_EXIT_USAGE;
    }

    if (options.verbose && options.quiet) {
        fputs("photoc: --verbose and --quiet cannot be used together\n", stderr);
        return PHOTOC_EXIT_USAGE;
    }

    const photoc_command *command = NULL;
    if (options.command != NULL) {
        command = find_command(options.command);
        if (command == NULL) {
            fprintf(stderr, "photoc: unknown command '%s'\n"
                            "Try 'photoc --help' for available commands.\n",
                    options.command);
            return PHOTOC_EXIT_USAGE;
        }
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
              "Try 'photoc --help' for available commands.\n", stderr);
        return PHOTOC_EXIT_USAGE;
    }

    if (strcmp(command->name, "exif") == 0) {
        if (options.argument_count != 1) {
            fputs("photoc exif: expected exactly one file\n"
                  "Usage: photoc exif <file>\n", stderr);
            return PHOTOC_EXIT_USAGE;
        }
        return photoc_command_exif(options.first_argument, options.json);
    }

    if (strcmp(command->name, "stats") == 0) {
        if (options.argument_count != 1) {
            fputs("photoc stats: expected exactly one directory\n"
                  "Usage: photoc stats <directory> [--recursive] [--json]\n", stderr);
            return PHOTOC_EXIT_USAGE;
        }
        return photoc_command_stats(options.first_argument, options.recursive,
                                    options.json);
    }

    return photoc_command_unimplemented(command->name);
}
