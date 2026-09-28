#include "photoc/json.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    FILE *stream = tmpfile();
    if (stream == NULL) {
        perror("tmpfile");
        return 1;
    }

    const char sample[] = {'A',        '"',        '\\', '\b', '\f',
                           '\n',       '\r',       '\t', 0x01, (char)0xc3,
                           (char)0xa9, (char)0xff, '\0'};
    const char expected[] =
        "\"A\\\"\\\\\\b\\f\\n\\r\\t\\u0001\xc3\xa9\\ufffd\"\nnull";

    int failed = photoc_json_write_string(stream, sample) != 0 ||
                 fputc('\n', stream) == EOF ||
                 photoc_json_write_string(stream, NULL) != 0 ||
                 fflush(stream) != 0 || fseek(stream, 0, SEEK_SET) != 0;
    char actual[128] = {0};
    if (!failed) {
        size_t count = fread(actual, 1, sizeof(actual) - 1, stream);
        actual[count] = '\0';
        failed = ferror(stream) || strcmp(actual, expected) != 0;
    }
    if (fclose(stream) != 0) {
        failed = 1;
    }
    if (failed) {
        fprintf(stderr, "JSON string escaping failed: %s\n", actual);
        return 1;
    }
    return 0;
}
