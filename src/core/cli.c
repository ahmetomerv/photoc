#include "photoc/cli.h"

#include "photoc/commands.h"
#include "photoc/version.h"

#include <stdio.h>
#include <string.h>

static const char *const commands[] = {
    "compress", "exif", "duplicates", "stats",
    "rename", "sort", "focus", "scrub"
};

static void print_help(void)
{
    puts("Usage: photoc <command> [args]");
    puts("       photoc --help");
    puts("       photoc --version");
    puts("");
    puts("Commands (not implemented yet):");
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        printf("  %s\n", commands[i]);
    }
}

int photoc_run(int argc, char *argv[])
{
    if (argc < 2) {
        fputs("Usage: photoc <command> [args]\n"
              "Try 'photoc --help' for available commands.\n", stderr);
        return PHOTOC_EXIT_USAGE;
    }

    const char *const arg = argv[1];

    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
        if (argc != 2) {
            fputs("photoc: --help takes no arguments\n", stderr);
            return PHOTOC_EXIT_USAGE;
        }
        print_help();
        return 0;
    }

    if (strcmp(arg, "--version") == 0 || strcmp(arg, "-V") == 0) {
        if (argc != 2) {
            fputs("photoc: --version takes no arguments\n", stderr);
            return PHOTOC_EXIT_USAGE;
        }
        puts("photoc " PHOTOC_VERSION);
        return 0;
    }

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (strcmp(arg, commands[i]) == 0) {
            return photoc_command_unimplemented(arg);
        }
    }

    fprintf(stderr, "photoc: unknown command '%s'\n"
                    "Try 'photoc --help' for available commands.\n", arg);
    return PHOTOC_EXIT_USAGE;
}
