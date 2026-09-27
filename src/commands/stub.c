#include "photoc/commands.h"

#include "photoc/cli.h"

#include <stdio.h>

int photoc_command_unimplemented(const char *name)
{
    fprintf(stderr, "photoc: %s is not implemented yet\n", name);
    return PHOTOC_EXIT_UNIMPLEMENTED;
}
