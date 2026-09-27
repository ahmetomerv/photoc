#ifndef PHOTOC_CLI_H
#define PHOTOC_CLI_H

enum {
    PHOTOC_EXIT_USAGE = 2,
    PHOTOC_EXIT_UNIMPLEMENTED = 3
};

int photoc_run(int argc, char *argv[]);

#endif
