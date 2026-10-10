#include "photoc/filename_template.h"

#include <math.h>
#include <stdint.h>
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

static char *copy_text(const char *text)
{
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, text, length + 1);
    }
    return copy;
}

static int make_photo(Photo *photo)
{
    if (photo_init(photo, "photos/My Shot.JpEg") != 0) {
        return -1;
    }
    photo->camera_model = copy_text("EOS/R5");
    photo->camera_make = copy_text("Canon");
    photo->capture_timestamp = copy_text("2026:09:27 12:34:56");
    if (photo->camera_model == NULL || photo->camera_make == NULL ||
        photo->capture_timestamp == NULL) {
        photo_cleanup(photo);
        return -1;
    }
    photo->iso = 200;
    photo->has_iso = true;
    photo->aperture = 2.8;
    photo->has_aperture = true;
    photo->focal_length = 50.0;
    photo->has_focal_length = true;
    return 0;
}

static void expect_name(const char *pattern, const Photo *photo,
                        uint64_t sequence, unsigned int width,
                        const char *expected)
{
    char *name = NULL;
    size_t offset = 0;
    photoc_template_result result = photoc_filename_template_expand(
        pattern, photo, sequence, width, &name, &offset);
    CHECK(result == PHOTOC_TEMPLATE_OK);
    if (result == PHOTOC_TEMPLATE_OK) {
        CHECK(name != NULL && strcmp(name, expected) == 0);
        CHECK(offset == SIZE_MAX);
    }
    free(name);
}

static void expect_error(const char *pattern, const Photo *photo,
                         photoc_template_result expected,
                         size_t expected_offset)
{
    char *name = NULL;
    size_t offset = 0;
    photoc_template_result result =
        photoc_filename_template_expand(pattern, photo, 7, 4, &name, &offset);
    CHECK(result == expected);
    CHECK(name == NULL);
    CHECK(offset == expected_offset);
    free(name);
}

/* The compiled pipeline must return exactly what the one-shot API returns for
   every pattern, including compile errors and missing-value render errors. */
static void expect_compiled_matches(const char *pattern, const Photo *photo,
                                    uint64_t sequence, unsigned int width)
{
    char *one_shot = NULL;
    size_t one_shot_offset = SIZE_MAX;
    photoc_template_result one_shot_result = photoc_filename_template_expand(
        pattern, photo, sequence, width, &one_shot, &one_shot_offset);

    photoc_filename_template *compiled = NULL;
    size_t compile_offset = SIZE_MAX;
    photoc_template_result compile_result =
        photoc_filename_template_compile(pattern, &compiled, &compile_offset);

    char *rendered = NULL;
    size_t render_offset = SIZE_MAX;
    photoc_template_result render_result = PHOTOC_TEMPLATE_INVALID_ARGUMENT;
    if (compile_result == PHOTOC_TEMPLATE_OK) {
        render_result = photoc_filename_template_render(
            compiled, photo, sequence, width, &rendered, &render_offset);
    }
    photoc_filename_template_free(compiled);

    if (compile_result != PHOTOC_TEMPLATE_OK) {
        CHECK(one_shot_result == compile_result);
        CHECK(one_shot == NULL);
        CHECK(one_shot_offset == compile_offset);
    } else {
        CHECK(render_result == one_shot_result);
        if (one_shot_result == PHOTOC_TEMPLATE_OK) {
            CHECK(one_shot != NULL && rendered != NULL);
            if (one_shot != NULL && rendered != NULL) {
                CHECK(strcmp(one_shot, rendered) == 0);
            }
            CHECK(one_shot_offset == SIZE_MAX && render_offset == SIZE_MAX);
        } else {
            CHECK(one_shot == NULL && rendered == NULL);
            CHECK(one_shot_offset == render_offset);
        }
    }
    free(one_shot);
    free(rendered);
}

static void test_compiled_matches(Photo *photo)
{
    static const char *const patterns[] = {
        "{date}_{camera}_{sequence}.{ext}",
        "{datetime}_{make}_{iso}_{aperture}_{focal}_{original}.{ext}",
        "{sequence}",
        "{{{original}}}",
        "literal",
        "{original}.{ext}",
        "a/b\\c:d*e?f\"g<h>i|j\nk\t",
        "{unknown}",
        "prefix_{Camera}",
        "a{date",
        "a{date{ext}}",
        "{}",
        "a}",
        "",
        ".",
        "..",
        "{{}}",
        "x",
        "{ext}",
        "{date}",
        "{camera}",
        "{make}",
        "{iso}",
        "{aperture}",
        "{focal}",
        "{datetime}",
        "{original}",
    };
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        expect_compiled_matches(patterns[i], photo, 7, 4);
        expect_compiled_matches(patterns[i], photo, 0, 0);
        expect_compiled_matches(patterns[i], photo, UINT64_MAX, 20);
    }

    /* A minimal photo exercises missing-value render errors on both paths. */
    Photo bare = {0};
    CHECK(photo_init(&bare, "photos/no_extension") == 0);
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        expect_compiled_matches(patterns[i], &bare, 1, 3);
    }
    photo_cleanup(&bare);
}

static void test_expansion(Photo *photo)
{
    expect_name("{date}_{camera}_{sequence}.{ext}", photo, 7, 4,
                "2026-09-27_EOS_R5_0007.JpEg");
    expect_name("{datetime}_{make}_{iso}_{aperture}_{focal}_{original}.{ext}",
                photo, 1, 0,
                "2026-09-27_12-34-56_Canon_200_2.8_50_My Shot.JpEg");
    expect_name("{sequence}", photo, 42, 0, "42");
    expect_name("{sequence}", photo, 42, 2, "42");
    expect_name("{sequence}", photo, 42, 5, "00042");
    expect_name("{sequence}", photo, UINT64_MAX, 20, "18446744073709551615");
    expect_name("{{{original}}}", photo, 1, 0, "{My Shot}");
    expect_name("literal", photo, 1, 0, "literal");
    expect_name("{original}.{ext}", photo, 1, 0, "My Shot.JpEg");
}

static void test_sanitization(Photo *photo)
{
    expect_name("a/b\\c:d*e?f\"g<h>i|j\nk\t", photo, 1, 0,
                "a_b_c_d_e_f_g_h_i_j_k_");
    free(photo->camera_make);
    photo->camera_make = copy_text("Nikon/Co:*?\"<>|\\\n");
    CHECK(photo->camera_make != NULL);
    if (photo->camera_make != NULL) {
        expect_name("{make}", photo, 1, 0, "Nikon_Co_________");
    }
}

static void test_errors(Photo *photo)
{
    size_t validation_offset = 0;
    CHECK(photoc_filename_template_validate(
              "{date}_{camera}", &validation_offset) == PHOTOC_TEMPLATE_OK);
    CHECK(validation_offset == SIZE_MAX);
    CHECK(photoc_filename_template_validate("x{unknown}", &validation_offset) ==
          PHOTOC_TEMPLATE_UNKNOWN_PLACEHOLDER);
    CHECK(validation_offset == 1);
    CHECK(photoc_filename_template_validate("x{date", &validation_offset) ==
          PHOTOC_TEMPLATE_INVALID_TEMPLATE);
    CHECK(validation_offset == 1);
    expect_error("{unknown}", photo, PHOTOC_TEMPLATE_UNKNOWN_PLACEHOLDER, 0);
    expect_error("prefix_{Camera}", photo, PHOTOC_TEMPLATE_UNKNOWN_PLACEHOLDER,
                 7);
    expect_error("a{date", photo, PHOTOC_TEMPLATE_INVALID_TEMPLATE, 1);
    expect_error("a{date{ext}}", photo, PHOTOC_TEMPLATE_INVALID_TEMPLATE, 1);
    expect_error("{}", photo, PHOTOC_TEMPLATE_INVALID_TEMPLATE, 0);
    expect_error("a}", photo, PHOTOC_TEMPLATE_INVALID_TEMPLATE, 1);
    expect_error("", photo, PHOTOC_TEMPLATE_INVALID_TEMPLATE, SIZE_MAX);
    expect_error(".", photo, PHOTOC_TEMPLATE_INVALID_TEMPLATE, SIZE_MAX);
    expect_error("..", photo, PHOTOC_TEMPLATE_INVALID_TEMPLATE, SIZE_MAX);

    free(photo->capture_timestamp);
    photo->capture_timestamp = NULL;
    expect_error("a{date}", photo, PHOTOC_TEMPLATE_MISSING_VALUE, 1);
    expect_error("{datetime}", photo, PHOTOC_TEMPLATE_MISSING_VALUE, 0);
    photo->capture_timestamp = copy_text("2026:02:30 12:34:56");
    CHECK(photo->capture_timestamp != NULL);
    expect_error("{date}", photo, PHOTOC_TEMPLATE_MISSING_VALUE, 0);

    free(photo->camera_model);
    photo->camera_model = NULL;
    expect_error("{camera}", photo, PHOTOC_TEMPLATE_MISSING_VALUE, 0);
    free(photo->camera_make);
    photo->camera_make = copy_text("");
    CHECK(photo->camera_make != NULL);
    expect_error("{make}", photo, PHOTOC_TEMPLATE_MISSING_VALUE, 0);
    photo->has_iso = false;
    expect_error("{iso}", photo, PHOTOC_TEMPLATE_MISSING_VALUE, 0);
    photo->has_aperture = true;
    photo->aperture = NAN;
    expect_error("{aperture}", photo, PHOTOC_TEMPLATE_MISSING_VALUE, 0);
    photo->focal_length = INFINITY;
    expect_error("{focal}", photo, PHOTOC_TEMPLATE_MISSING_VALUE, 0);

    Photo no_extension = {0};
    CHECK(photo_init(&no_extension, "photos/no_extension") == 0);
    expect_name("{original}", &no_extension, 1, 0, "no_extension");
    expect_error("{ext}", &no_extension, PHOTOC_TEMPLATE_MISSING_VALUE, 0);
    photo_cleanup(&no_extension);

    char *name = NULL;
    CHECK(photoc_filename_template_expand("x", photo, 1, 21, &name, NULL) ==
          PHOTOC_TEMPLATE_INVALID_ARGUMENT);
    CHECK(name == NULL);
    CHECK(photoc_filename_template_expand(NULL, photo, 1, 0, &name, NULL) ==
          PHOTOC_TEMPLATE_INVALID_ARGUMENT);
    CHECK(name == NULL);
    CHECK(photoc_filename_template_expand("x", NULL, 1, 0, &name, NULL) ==
          PHOTOC_TEMPLATE_INVALID_ARGUMENT);
    CHECK(name == NULL);
    CHECK(photoc_filename_template_expand("x", photo, 1, 0, NULL, NULL) ==
          PHOTOC_TEMPLATE_INVALID_ARGUMENT);
    CHECK(strstr(photoc_template_result_message(
                     PHOTOC_TEMPLATE_UNKNOWN_PLACEHOLDER),
                 "unknown placeholder") != NULL);
}

static void test_long_value(Photo *photo)
{
    char *long_model = malloc(8193);
    CHECK(long_model != NULL);
    if (long_model == NULL) {
        return;
    }
    memset(long_model, 'A', 8192);
    long_model[8192] = '\0';
    free(photo->camera_model);
    photo->camera_model = long_model;

    char *name = NULL;
    CHECK(photoc_filename_template_expand("{camera}.{ext}", photo, 1, 0, &name,
                                          NULL) == PHOTOC_TEMPLATE_OK);
    if (name != NULL) {
        CHECK(strlen(name) == 8197);
        CHECK(strcmp(name + 8192, ".JpEg") == 0);
    }
    free(name);
}

int main(void)
{
    Photo photo = {0};
    if (make_photo(&photo) != 0) {
        fputs("unable to create template test photo\n", stderr);
        return 1;
    }
    test_expansion(&photo);
    test_sanitization(&photo);
    test_errors(&photo);
    test_long_value(&photo);
    test_compiled_matches(&photo);
    photo_cleanup(&photo);
    if (failures != 0) {
        fprintf(stderr, "%d filename-template test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
