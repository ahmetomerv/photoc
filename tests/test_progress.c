#include "photoc/progress.h"

#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t fake_clock(void *data)
{
    return *(uint64_t *)data;
}

static size_t contents(FILE *file, char *buffer, size_t capacity)
{
    fflush(file);
    long position = ftell(file);
    assert(position >= 0);
    assert(fseek(file, 0, SEEK_SET) == 0);
    size_t size = fread(buffer, 1, capacity - 1, file);
    buffer[size] = '\0';
    assert(fseek(file, position, SEEK_SET) == 0);
    return size;
}

int main(void)
{
    char buffer[512];
    assert(photoc_progress_format(buffer, sizeof(buffer), "⠋",
                                  "Discovering photos...", 0, 0, false) > 0);
    assert(strcmp(buffer, "⠋ Discovering photos...") == 0);
    assert(photoc_progress_format(buffer, sizeof(buffer), "⠸", "Analyzing...",
                                  42, 100, true) > 0);
    assert(strcmp(buffer, "⠸ Analyzing... 42 / 100 (42%)") == 0);
    photoc_progress_format(buffer, sizeof(buffer), "⠋", "Work", 0, 100, true);
    assert(strstr(buffer, "(0%)") != NULL);
    photoc_progress_format(buffer, sizeof(buffer), "⠋", "Work", 50, 100, true);
    assert(strstr(buffer, "(50%)") != NULL);
    photoc_progress_format(buffer, sizeof(buffer), "⠋", "Work", 100, 100, true);
    assert(strstr(buffer, "(100%)") != NULL);
    photoc_progress_format(buffer, sizeof(buffer), "⠋", "Work", 0, 0, true);
    assert(strstr(buffer, "(100%)") != NULL);
    photoc_progress_format(buffer, sizeof(buffer), "⠋", "Work", 1924, 6284,
                           true);
    assert(strstr(buffer, "1,924 / 6,284") != NULL);
    photoc_progress_format_elapsed(buffer, sizeof(buffer), 823);
    assert(strcmp(buffer, "823ms") == 0);
    photoc_progress_format_elapsed(buffer, sizeof(buffer), 3700);
    assert(strcmp(buffer, "3.7s") == 0);
    photoc_progress_format_elapsed(buffer, sizeof(buffer), 74000);
    assert(strcmp(buffer, "1m 14s") == 0);

    FILE *file = tmpfile();
    assert(file != NULL);
    uint64_t now = 1000;
    photoc_progress progress;
    photoc_progress_init_test(&progress, file, true, PHOTOC_PROGRESS_AUTO,
                              false, "Scanning...", fake_clock, &now);
    assert(progress.enabled);
    photoc_progress_start(&progress);
    now = 1199;
    photoc_progress_update(&progress, 1);
    assert(contents(file, buffer, sizeof(buffer)) == 0);
    now = 1200;
    photoc_progress_update(&progress, 2);
    size_t first = contents(file, buffer, sizeof(buffer));
    assert(strstr(buffer, "⠋ Scanning...") != NULL);
    now = 1250;
    photoc_progress_update(&progress, 3);
    assert(contents(file, buffer, sizeof(buffer)) == first);
    now = 1290;
    photoc_progress_set_message(&progress, "Analyzing...");
    photoc_progress_set_total(&progress, 100);
    now = 1380;
    photoc_progress_update(&progress, 42);
    assert(contents(file, buffer, sizeof(buffer)) > first);
    assert(strstr(buffer, "42 / 100 (42%)") != NULL);
    photoc_progress_finish(&progress, "Analyzed 42 photos");
    contents(file, buffer, sizeof(buffer));
    assert(strstr(buffer, "✓ Analyzed 42 photos in 380ms\n") != NULL);
    fclose(file);

    file = tmpfile();
    assert(file != NULL);
    now = 0;
    photoc_progress_init_test(&progress, file, true, PHOTOC_PROGRESS_AUTO,
                              false, "Work", fake_clock, &now);
    photoc_progress_start(&progress);
    now = 200;
    photoc_progress_update(&progress, 1);
    photoc_progress_before_diagnostic(&progress);
    fputs("warning: skipped one file\n", file);
    now = 290;
    photoc_progress_update(&progress, 2);
    photoc_progress_warn(&progress, "Analyzed 2 photos; 1 skipped");
    contents(file, buffer, sizeof(buffer));
    assert(strstr(buffer, "warning: skipped one file\n") != NULL);
    assert(strstr(buffer, "! Analyzed 2 photos; 1 skipped\n") != NULL);
    assert(strstr(buffer, "skipped in") == NULL);
    fclose(file);

    file = tmpfile();
    assert(file != NULL);
    now = 0;
    photoc_progress_init_test(&progress, file, true, PHOTOC_PROGRESS_AUTO,
                              false, "Quick...", fake_clock, &now);
    photoc_progress_start(&progress);
    now = 80;
    photoc_progress_finish(&progress, "Done");
    assert(contents(file, buffer, sizeof(buffer)) == 0);
    fclose(file);

    file = tmpfile();
    assert(file != NULL);
    photoc_progress_init_test(&progress, file, false, PHOTOC_PROGRESS_AUTO,
                              false, "Work", fake_clock, &now);
    assert(!progress.enabled);
    photoc_progress_start(&progress);
    now += 500;
    photoc_progress_update(&progress, 5);
    photoc_progress_finish(&progress, "Done");
    assert(contents(file, buffer, sizeof(buffer)) == 0);
    photoc_progress_init_test(&progress, file, true, PHOTOC_PROGRESS_NEVER,
                              false, "Work", fake_clock, &now);
    assert(!progress.enabled);
    photoc_progress_init_test(&progress, file, true, PHOTOC_PROGRESS_AUTO, true,
                              "Work", fake_clock, &now);
    assert(!progress.enabled);
    fclose(file);
    return 0;
}
