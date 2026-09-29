#define _POSIX_C_SOURCE 200809L

#include "photoc/query.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static void set_text(char **field, const char *text)
{
    free(*field);
    *field = text == NULL ? NULL : strdup(text);
    CHECK(text == NULL || *field != NULL);
}

static void test_comparators(void)
{
    static const struct {
        const char *text;
        bool below;
        bool equal;
        bool above;
    } cases[] = {{"100", false, true, false},  {"=100", false, true, false},
                 {">100", false, false, true}, {">=100", false, true, true},
                 {"<100", true, false, false}, {"<=100", true, true, false}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        photoc_comparison comparison;
        CHECK(photoc_comparison_parse(cases[i].text, &comparison));
        CHECK(photoc_comparison_matches(&comparison, 99) == cases[i].below);
        CHECK(photoc_comparison_matches(&comparison, 100) == cases[i].equal);
        CHECK(photoc_comparison_matches(&comparison, 101) == cases[i].above);
        CHECK(!photoc_comparison_matches(&comparison, NAN));
        CHECK(!photoc_comparison_matches(&comparison, INFINITY));
    }
    static const char *const invalid[] = {"",      " ",      "=",       ">",
                                          "<",     "<=",     ">=",      "==100",
                                          "=>100", "=<100",  ">>100",   "!=100",
                                          "100x",  "1 00",   " 100",    "100 ",
                                          "> 100", "-1",     "nan",     "inf",
                                          "0x10",  "1e9999", "1e-9999", "..",
                                          ">=-0"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        photoc_comparison comparison = {PHOTOC_COMPARE_LESS, 17};
        CHECK(!photoc_comparison_parse(invalid[i], &comparison));
        CHECK(comparison.relation == PHOTOC_COMPARE_LESS &&
              comparison.value == 17);
    }
    photoc_comparison comparison;
    CHECK(!photoc_comparison_parse(NULL, &comparison));
    CHECK(!photoc_comparison_parse("1", NULL));
    CHECK(!photoc_comparison_matches(NULL, 1));
    CHECK(photoc_comparison_parse("<=2.8", &comparison));
    CHECK(photoc_comparison_matches(&comparison, 2.8));
    CHECK(photoc_comparison_parse(">=1e3", &comparison));
    CHECK(photoc_comparison_matches(&comparison, 1000));
}

static void test_predicates(void)
{
    Photo photo = {.path = strdup("fixture.jpg"),
                   .camera_make = strdup("Sony"),
                   .camera_model = strdup("DSC-RX100M7A"),
                   .has_iso = true,
                   .iso = 1600,
                   .has_aperture = true,
                   .aperture = 4,
                   .has_focal_length = true,
                   .focal_length = 50,
                   .capture_timestamp = strdup("2026:12:31 23:59:59"),
                   .has_gps = true};
    photoc_query_filters filters = {.camera = "DSC-RX100M7A",
                                    .make = "Sony",
                                    .iso = ">800",
                                    .aperture = "<=4",
                                    .focal = ">=50",
                                    .after = "2026-01-01",
                                    .before = "2026-12-31",
                                    .has_gps = true};
    photoc_query query;
    const char *invalid;
    CHECK(photoc_query_init(&filters, &query, &invalid));
    CHECK(invalid == NULL && photoc_query_matches(&query, &photo));
    photo.has_iso = false;
    CHECK(!photoc_query_matches(&query, &photo));
    photo.has_iso = true;
    photo.iso = 800;
    CHECK(!photoc_query_matches(&query, &photo));
    photo.iso = 1600;
    photo.has_aperture = false;
    CHECK(!photoc_query_matches(&query, &photo));
    photo.has_aperture = true;
    photo.has_focal_length = false;
    CHECK(!photoc_query_matches(&query, &photo));
    photo.has_focal_length = true;
    set_text(&photo.camera_model, NULL);
    CHECK(!photoc_query_matches(&query, &photo));
    set_text(&photo.camera_model, "dsc-rx100m7a");
    CHECK(!photoc_query_matches(&query, &photo));
    set_text(&photo.camera_model, "DSC-RX100M7A");
    set_text(&photo.camera_make, NULL);
    CHECK(!photoc_query_matches(&query, &photo));
    set_text(&photo.camera_make, "Sony");
    photo.has_gps = false;
    CHECK(!photoc_query_matches(&query, &photo));
    photo.has_gps = true;
    set_text(&photo.capture_timestamp, "2026:01:01 00:00:00");
    CHECK(photoc_query_matches(&query, &photo));
    set_text(&photo.capture_timestamp, "2025:12:31 23:59:59");
    CHECK(!photoc_query_matches(&query, &photo));
    set_text(&photo.capture_timestamp, "2027:01:01 00:00:00");
    CHECK(!photoc_query_matches(&query, &photo));
    set_text(&photo.capture_timestamp, "2026:02:30 00:00:00");
    CHECK(!photoc_query_matches(&query, &photo));
    set_text(&photo.capture_timestamp, NULL);
    CHECK(!photoc_query_matches(&query, &photo));
    filters = (photoc_query_filters){.no_gps = true};
    CHECK(photoc_query_init(&filters, &query, &invalid));
    CHECK(!photoc_query_matches(&query, &photo));
    photo_cleanup(&photo);
    CHECK(photoc_query_matches(&query, &photo));
    filters = (photoc_query_filters){.iso = "<100"};
    CHECK(photoc_query_init(&filters, &query, &invalid));
    CHECK(!photoc_query_matches(&query, &photo));
    filters = (photoc_query_filters){0};
    CHECK(photoc_query_init(&filters, &query, &invalid));
    CHECK(photoc_query_matches(&query, &photo));
    CHECK(!photoc_query_matches(NULL, &photo));
    CHECK(!photoc_query_matches(&query, NULL));
    photo_cleanup(&photo);
}

static void test_dates_and_validation(void)
{
    static const char *const invalid_dates[] = {
        "",           "2026-1-01",  "2026:01:01",         "2026-01-01x",
        "2026-02-29", "1900-02-29", "0000-01-01",         "2026-13-01",
        "2026-01-00", "2026-04-31", "2026-01-01 12:00:00"};
    photoc_query query = {.first_day = 123};
    for (size_t i = 0; i < sizeof(invalid_dates) / sizeof(invalid_dates[0]);
         ++i) {
        photoc_query_filters filters = {.after = invalid_dates[i]};
        const char *invalid = NULL;
        CHECK(!photoc_query_init(&filters, &query, &invalid));
        CHECK(strcmp(invalid, "--after") == 0 && query.first_day == 123);
        filters = (photoc_query_filters){.before = invalid_dates[i]};
        CHECK(!photoc_query_init(&filters, &query, &invalid));
        CHECK(strcmp(invalid, "--before") == 0 && query.first_day == 123);
    }
    photoc_query_filters filters = {.after = "2000-02-29",
                                    .before = "2000-02-29"};
    CHECK(photoc_query_init(&filters, &query, NULL));
    Photo photo = {.path = strdup("fixture.jpg"),
                   .capture_timestamp = strdup("2000:02:29 23:59:59")};
    CHECK(photoc_query_matches(&query, &photo));
    filters =
        (photoc_query_filters){.after = "2026-12-31", .before = "2026-01-01"};
    CHECK(!photoc_query_init(&filters, &query, NULL));
    filters = (photoc_query_filters){.has_gps = true, .no_gps = true};
    CHECK(!photoc_query_init(&filters, &query, NULL));
    filters = (photoc_query_filters){.camera = ""};
    CHECK(!photoc_query_init(&filters, &query, NULL));
    filters = (photoc_query_filters){.make = ""};
    CHECK(!photoc_query_init(&filters, &query, NULL));
    CHECK(!photoc_query_init(NULL, &query, NULL));
    CHECK(!photoc_query_init(&filters, NULL, NULL));
    filters =
        (photoc_query_filters){.after = "0001-01-01", .before = "9999-12-31"};
    CHECK(photoc_query_init(&filters, &query, NULL));
    set_text(&photo.capture_timestamp, "9999:12:31 23:59:59");
    CHECK(photoc_query_matches(&query, &photo));
    photo_cleanup(&photo);
}

int main(void)
{
    test_comparators();
    test_predicates();
    test_dates_and_validation();
    return failures == 0 ? 0 : 1;
}
