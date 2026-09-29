#ifndef PHOTOC_QUERY_H
#define PHOTOC_QUERY_H

#include "photoc/photo.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PHOTOC_COMPARE_EQUAL,
    PHOTOC_COMPARE_GREATER,
    PHOTOC_COMPARE_GREATER_EQUAL,
    PHOTOC_COMPARE_LESS,
    PHOTOC_COMPARE_LESS_EQUAL
} photoc_compare_operator;

typedef struct {
    photoc_compare_operator relation;
    double value;
} photoc_comparison;

/* Accepts an optional =, >, >=, <, or <= followed by a finite nonnegative
   number using the existing decimal parser. No whitespace is allowed.
   Leaves comparison unchanged on failure. No strings or memory are owned. */
bool photoc_comparison_parse(const char *expression,
                             photoc_comparison *comparison);
bool photoc_comparison_matches(const photoc_comparison *comparison,
                               double value);

/* All strings are borrowed. NULL means no filter; GPS flags are exclusive. */
typedef struct {
    const char *camera;
    const char *make;
    const char *iso;
    const char *aperture;
    const char *focal;
    const char *after;
    const char *before;
    bool has_gps;
    bool no_gps;
} photoc_query_filters;

typedef struct {
    photoc_query_filters filters;
    photoc_comparison iso;
    photoc_comparison aperture;
    photoc_comparison focal;
    uint64_t first_day; /* Inclusive midnight in local EXIF time. */
    uint64_t last_day;  /* Inclusive last second in local EXIF time. */
} photoc_query;

/* Compiles filters without allocation. Date bounds include the named days;
   string filters are exact and case-sensitive. On failure query is unchanged
   and invalid_filter receives a static option name (never free it). On success
   query borrows filters' strings for its lifetime. */
bool photoc_query_init(const photoc_query_filters *filters, photoc_query *query,
                       const char **invalid_filter);

/* ANDs every supplied filter. Missing/invalid queried fields do not match.
   no_gps means no valid coordinate pair, including absent/invalid GPS tags.
   Does not mutate or retain photo, and never performs I/O. */
bool photoc_query_matches(const photoc_query *query, const Photo *photo);

#endif
