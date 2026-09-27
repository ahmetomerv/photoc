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
} photoc_command;

static const photoc_command commands[] = {
    {"compress", "Compress image files"},
    {"exif", "Inspect image metadata"},
    {"duplicates", "Find duplicate photos"},
    {"stats", "Summarize photo collections"},
    {"rename", "Rename photo files"},
    {"sort", "Organize photos into folders"},
    {"focus", "Assess image focus"},
    {"scrub", "Remove selected metadata"}
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
    puts("Usage: photoc [global options] <command> [args]");
    puts("       photoc --help");
    puts("       photoc --version");
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
}

static void print_command_help(const photoc_command *command)
{
    printf("Usage: photoc [global options] %s [args]\n\n", command->name);
    puts(command->description);
    puts("This command is not implemented yet.");
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
        fputs("Usage: photoc [global options] <command> [args]\n"
              "Try 'photoc --help' for available commands.\n", stderr);
        return PHOTOC_EXIT_USAGE;
    }

    return photoc_command_unimplemented(command->name);
}
