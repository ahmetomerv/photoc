#include "photoc/parse.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static void test_values(void)
{
    const char *valid[] = {"0",  "100", "12.25", ".5",
                           "1.", "+2",  "1e2",   "1E-2"};
    double expected[] = {0, 100, 12.25, 0.5, 1, 2, 100, 0.01};
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        double value = -17;
        CHECK(photoc_parse_nonnegative_double(valid[i], &value));
        CHECK(value == expected[i]);
    }
    const char *invalid[] = {
        NULL,       "",      "-1",    "-0",   "nan",    "NaN",     "inf",
        "Infinity", "0x10",  "0x1p2", " 100", "100 ",   "1\n",     ".",
        "+",        "1.2.3", "1e",    "1e+",  "1e9999", "1e-9999", "100px"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        double value = -17;
        CHECK(!photoc_parse_nonnegative_double(invalid[i], &value));
        CHECK(value == -17);
    }
    CHECK(!photoc_parse_nonnegative_double("100", NULL));
}

static void test_routing(void)
{
    char program[] = "photoc";
    char focus[] = "focus";
    char threshold[] = "--threshold";
    char value[] = "12.5";
    char only_blurry[] = "--only-blurry";
    char photos[] = "photos";
    char recursive[] = "--recursive";
    char one[] = "1";
    char two[] = "2";
    char stats[] = "stats";
    char ten[] = "10";
    char end[] = "--";
    char *argv[] = {program, threshold, value,    only_blurry,
                    focus,   photos,    recursive};
    photoc_cli_options options;
    const char *error;
    CHECK(photoc_parse_args(7, argv, &options, &error) == PHOTOC_PARSE_OK);
    CHECK(error == NULL);
    CHECK(strcmp(options.command, "focus") == 0);
    CHECK(strcmp(options.first_argument, "photos") == 0);
    CHECK(options.argument_count == 1);
    CHECK(strcmp(options.threshold, "12.5") == 0);
    CHECK(options.only_blurry && options.recursive);

    char *duplicate[] = {program, focus, threshold, one, threshold, two};
    CHECK(photoc_parse_args(6, duplicate, &options, &error) ==
          PHOTOC_PARSE_DUPLICATE_OPTION);
    CHECK(strcmp(error, "--threshold") == 0);
    char *missing[] = {program, focus, threshold, only_blurry};
    CHECK(photoc_parse_args(4, missing, &options, &error) ==
          PHOTOC_PARSE_MISSING_VALUE);
    char *wrong_command[] = {program, stats, threshold, ten};
    CHECK(photoc_parse_args(4, wrong_command, &options, &error) ==
          PHOTOC_PARSE_UNKNOWN_OPTION);
    char *ended[] = {program, focus, end, threshold};
    CHECK(photoc_parse_args(4, ended, &options, &error) == PHOTOC_PARSE_OK);
    CHECK(options.threshold == NULL);
    CHECK(strcmp(options.first_argument, "--threshold") == 0);
}

int main(void)
{
    test_values();
    test_routing();
    return failures == 0 ? 0 : 1;
}
