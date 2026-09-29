#include "photoc/query.h"

#include "photoc/parse.h"
#include "photoc/timestamp.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

bool photoc_comparison_parse(const char *expression,
                             photoc_comparison *comparison)
{
    if (expression == NULL || comparison == NULL)
        return false;
    photoc_comparison parsed = {.relation = PHOTOC_COMPARE_EQUAL};
    const char *number = expression;
    if (*number == '=' || *number == '>' || *number == '<') {
        char symbol = *number++;
        bool equal = symbol != '=' && *number == '=';
        if (equal)
            ++number;
        if (symbol == '>')
            parsed.relation =
                equal ? PHOTOC_COMPARE_GREATER_EQUAL : PHOTOC_COMPARE_GREATER;
        else if (symbol == '<')
            parsed.relation =
                equal ? PHOTOC_COMPARE_LESS_EQUAL : PHOTOC_COMPARE_LESS;
    }
    if (!photoc_parse_nonnegative_double(number, &parsed.value))
        return false;
    *comparison = parsed;
    return true;
}

bool photoc_comparison_matches(const photoc_comparison *comparison,
                               double value)
{
    if (comparison == NULL || !isfinite(value) || !isfinite(comparison->value))
        return false;
    switch (comparison->relation) {
    case PHOTOC_COMPARE_EQUAL:
        return value == comparison->value;
    case PHOTOC_COMPARE_GREATER:
        return value > comparison->value;
    case PHOTOC_COMPARE_GREATER_EQUAL:
        return value >= comparison->value;
    case PHOTOC_COMPARE_LESS:
        return value < comparison->value;
    case PHOTOC_COMPARE_LESS_EQUAL:
        return value <= comparison->value;
    }
    return false;
}

static bool parse_date(const char *text, uint64_t *seconds)
{
    if (strlen(text) != 10 || text[4] != '-' || text[7] != '-')
        return false;
    char timestamp[20] = "0000:00:00 00:00:00";
    memcpy(timestamp, text, 10);
    timestamp[4] = ':';
    timestamp[7] = ':';
    return photoc_timestamp_to_seconds(timestamp, seconds);
}

bool photoc_query_init(const photoc_query_filters *filters, photoc_query *query,
                       const char **invalid_filter)
{
    if (invalid_filter != NULL)
        *invalid_filter = NULL;
    if (filters == NULL || query == NULL)
        return false;
    photoc_query parsed = {.filters = *filters};
    const char *invalid = NULL;
    if (filters->camera != NULL && filters->camera[0] == '\0')
        invalid = "--camera";
    else if (filters->make != NULL && filters->make[0] == '\0')
        invalid = "--make";
    else if (filters->iso != NULL &&
             !photoc_comparison_parse(filters->iso, &parsed.iso))
        invalid = "--iso";
    else if (filters->aperture != NULL &&
             !photoc_comparison_parse(filters->aperture, &parsed.aperture))
        invalid = "--aperture";
    else if (filters->focal != NULL &&
             !photoc_comparison_parse(filters->focal, &parsed.focal))
        invalid = "--focal";
    else if (filters->after != NULL &&
             !parse_date(filters->after, &parsed.first_day))
        invalid = "--after";
    else if (filters->before != NULL &&
             !parse_date(filters->before, &parsed.last_day))
        invalid = "--before";
    else if (filters->has_gps && filters->no_gps)
        invalid = "--has-gps/--no-gps";
    if (invalid == NULL && filters->after != NULL && filters->before != NULL &&
        parsed.first_day > parsed.last_day)
        invalid = "--after/--before";
    if (invalid != NULL) {
        if (invalid_filter != NULL)
            *invalid_filter = invalid;
        return false;
    }
    if (filters->before != NULL)
        parsed.last_day += 86399;
    *query = parsed;
    return true;
}

bool photoc_query_matches(const photoc_query *query, const Photo *photo)
{
    if (query == NULL || photo == NULL)
        return false;
    const photoc_query_filters *filters = &query->filters;
    if (filters->camera != NULL &&
        (photo->camera_model == NULL ||
         strcmp(filters->camera, photo->camera_model) != 0))
        return false;
    if (filters->make != NULL &&
        (photo->camera_make == NULL ||
         strcmp(filters->make, photo->camera_make) != 0))
        return false;
    if (filters->iso != NULL &&
        (!photo->has_iso ||
         !photoc_comparison_matches(&query->iso, photo->iso)))
        return false;
    if (filters->aperture != NULL &&
        (!photo->has_aperture ||
         !photoc_comparison_matches(&query->aperture, photo->aperture)))
        return false;
    if (filters->focal != NULL &&
        (!photo->has_focal_length ||
         !photoc_comparison_matches(&query->focal, photo->focal_length)))
        return false;
    if ((filters->has_gps && !photo->has_gps) ||
        (filters->no_gps && photo->has_gps))
        return false;
    if (filters->after != NULL || filters->before != NULL) {
        uint64_t timestamp;
        if (!photoc_timestamp_to_seconds(photo->capture_timestamp,
                                         &timestamp) ||
            (filters->after != NULL && timestamp < query->first_day) ||
            (filters->before != NULL && timestamp > query->last_day))
            return false;
    }
    return true;
}
