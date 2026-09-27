#ifndef PHOTOC_COMMANDS_H
#define PHOTOC_COMMANDS_H

#include <stdbool.h>

int photoc_command_unimplemented(const char *name);
int photoc_command_exif(const char *path, bool json);
int photoc_command_stats(const char *directory, bool recursive, bool json);
int photoc_command_rename(const char *directory, const char *format,
                          bool recursive, bool apply);
int photoc_command_sort(const char *directory, bool recursive);

#endif
