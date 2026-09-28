#include "photoc/cli.h"
#include "photoc/error.h"
#include "photoc/exit_codes.h"

#include <errno.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
    int result = photoc_run(argc, argv);
    if (fflush(stdout) == EOF || ferror(stdout)) {
        int error = errno == 0 ? EIO : errno;
        photoc_error_report(NULL, PHOTOC_ERR_NOTE_NONE, PHOTOC_ERR_IO, NULL,
                            "unable to write standard output", error);
        if (result == PHOTOC_EXIT_SUCCESS) {
            result = PHOTOC_EXIT_FAILURE;
        }
    }
    return result;
}
