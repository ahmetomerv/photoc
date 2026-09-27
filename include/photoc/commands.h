#ifndef PHOTOC_COMMANDS_H
#define PHOTOC_COMMANDS_H

#include <stdbool.h>

int photoc_command_unimplemented(const char *name);
int photoc_command_exif(const char *path, bool json);

#endif
