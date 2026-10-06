#define _POSIX_C_SOURCE 200809L

#include "photoc/commands.h"

#include "review_display.h"
#include "review_iterm.h"
#include "review_model.h"
#include "review_state.h"
#include "review_terminal.h"

#include "photoc/error.h"
#include "photoc/exit_codes.h"
#include "photoc/fs.h"
#include "photoc/photo.h"
#include "photoc/progress.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    review_model model;
    review_state state;
    review_terminal terminal;
    review_score_cache *scores; /* Indexed by model.items; owned. */
    bool show_images;
    bool color_actions;
    bool details;
    bool help;
    bool save_failed;
    char save_message[160];
} review_session;

static review_show parse_show(const char *show)
{
    if (strcmp(show, "unmarked") == 0)
        return PHOTOC_REVIEW_SHOW_UNMARKED;
    if (strcmp(show, "picked") == 0)
        return PHOTOC_REVIEW_SHOW_PICKED;
    if (strcmp(show, "rejected") == 0)
        return PHOTOC_REVIEW_SHOW_REJECTED;
    return PHOTOC_REVIEW_SHOW_ALL;
}

static const char *status_name(photoc_review_status status)
{
    switch (status) {
    case PHOTOC_REVIEW_UNMARKED: return "UNMARKED";
    case PHOTOC_REVIEW_PICKED: return "PICKED";
    case PHOTOC_REVIEW_REJECTED: return "REJECTED";
    }
    return "UNKNOWN";
}

static const char *show_name(review_show show)
{
    switch (show) {
    case PHOTOC_REVIEW_SHOW_ALL: return "matching";
    case PHOTOC_REVIEW_SHOW_UNMARKED: return "unmarked";
    case PHOTOC_REVIEW_SHOW_PICKED: return "picked";
    case PHOTOC_REVIEW_SHOW_REJECTED: return "rejected";
    }
    return "matching";
}

static void print_action(FILE *stream, bool color, const char *key,
                         const char *label, const char *key_style,
                         const char *label_style)
{
    if (color)
        fputs(key_style, stream);
    fprintf(stream, "[%s]", key);
    if (color)
        fputs("\033[0m", stream);
    fputc(' ', stream);
    if (color)
        fputs(label_style, stream);
    fputs(label, stream);
    if (color)
        fputs("\033[0m", stream);
}

static void render_controls(bool color)
{
    fputs("\nACTIONS  ", stdout);
    print_action(stdout, color, "P", "PICK", "\033[1;30;42m",
                 "\033[1;32m");
    fputs("    ", stdout);
    print_action(stdout, color, "X", "REJECT", "\033[1;37;41m",
                 "\033[1;31m");
    fputs("    ", stdout);
    print_action(stdout, color, "U", "UNMARK", "\033[1;30;43m",
                 "\033[1;33m");
    fputs("\nMOVE     [Left/H] Previous    [Right/L/Space] Next\n"
          "MORE     [I] Info  [?] Help  [Q] Quit\n", stdout);
}

static int render(review_session *session)
{
    if (fputs("\033[H\033[2Jphotoc review\n\n", stdout) == EOF)
        return -1;
    const review_item *item = review_model_current(&session->model);
    if (item == NULL) {
        fprintf(stdout, "No %s photos to review.\n", show_name(session->model.show));
    } else {
        review_terminal_print_safe(stdout, item->relative_path);
        fprintf(stdout, "    %zu / %zu\n%s\n\n", session->model.cursor + 1,
                session->model.visible_count, status_name(item->status));
        if (session->help) {
            fputs("Help\n"
                  "  Left/h: previous     Right/l/space: next\n"
                  "  p: pick             x: reject       u: unmark\n"
                  "  i: details          ?: help         q: quit\n", stdout);
        } else {
            size_t index = session->model.visible[session->model.cursor];
            review_score_cache *score = &session->scores[index];
            photoc_fs_type type;
            if (photoc_fs_get_type(item->path, &type) != 0 ||
                type != PHOTOC_FS_FILE) {
                review_score_fail(score, PHOTOC_IMAGE_IO_ERROR);
                fputs("Photo unavailable\n", stdout);
                fputs("Sharpness: unavailable\n", stdout);
            } else {
                Photo photo = {0};
                photoc_metadata_result metadata =
                    photo_load_metadata(item->path, &photo);
                if (metadata == PHOTOC_METADATA_IO_ERROR ||
                    metadata == PHOTOC_METADATA_INVALID_JPEG) {
                    review_score_fail(score,
                        metadata == PHOTOC_METADATA_IO_ERROR ?
                        PHOTOC_IMAGE_IO_ERROR : PHOTOC_IMAGE_INVALID_JPEG);
                    fputs("Photo unavailable\n", stdout);
                    fputs("Sharpness: unavailable\n", stdout);
                } else {
                    if (metadata == PHOTOC_METADATA_OK) {
                        if (session->show_images) {
                            review_image_result image =
                                review_iterm_render(stdout, item->path);
                            if (image == REVIEW_IMAGE_OUTPUT_ERROR) {
                                photo_cleanup(&photo);
                                return -1;
                            }
                            if (image == REVIEW_IMAGE_UNAVAILABLE)
                                fputs("Image unavailable\n", stdout);
                        }
                        review_display_metadata(stdout, &photo,
                                                session->details);
                        photo_cleanup(&photo);
                    } else {
                        fputs("Metadata unavailable\n", stdout);
                    }
                    review_score_get(score, item->path);
                    review_display_sharpness(stdout, score);
                }
            }
        }
    }
    fprintf(stdout, "\nPicked: %zu    Rejected: %zu    Unmarked: %zu    Total: %zu\n",
            session->model.picked, session->model.rejected,
            session->model.unmarked, session->model.count);
    if (session->save_message[0] != '\0')
        fprintf(stdout, "\nCould not save selection: %s\n", session->save_message);
    render_controls(session->color_actions);
    return fflush(stdout) == 0 && !ferror(stdout) ? 0 : -1;
}

static int handle_mark(review_session *session, photoc_review_status status)
{
    const review_item *item = review_model_current(&session->model);
    if (item == NULL)
        return 0;
    if (review_state_save_mark(&session->state, item->relative_path,
                               status) != 0) {
        session->save_failed = true;
        snprintf(session->save_message, sizeof(session->save_message),
                 "%s", strerror(errno));
        return render(session);
    }
    session->save_message[0] = '\0';
    if (review_model_mark_current(&session->model, status) != 0)
        return -1;
    return render(session);
}

static int run_terminal(review_session *session)
{
    if (review_terminal_enter(&session->terminal) != 0)
        return -1;
    int result = render(session);
    bool done = false;
    while (result == 0 && !done) {
        if (photoc_progress_interrupted() || review_terminal_hung_up()) {
            result = -1;
            break;
        }
        if (review_terminal_resized() && render(session) != 0) {
            result = -1;
            break;
        }
        review_key key;
        if (review_terminal_read(&session->terminal, &key) != 0) {
            result = -1;
            break;
        }
        switch (key) {
        case REVIEW_KEY_NONE: break;
        case REVIEW_KEY_NEXT:
            if (review_model_next(&session->model))
                result = render(session);
            break;
        case REVIEW_KEY_PREVIOUS:
            if (review_model_previous(&session->model))
                result = render(session);
            break;
        case REVIEW_KEY_PICK:
            result = handle_mark(session, PHOTOC_REVIEW_PICKED);
            break;
        case REVIEW_KEY_REJECT:
            result = handle_mark(session, PHOTOC_REVIEW_REJECTED);
            break;
        case REVIEW_KEY_UNMARK:
            result = handle_mark(session, PHOTOC_REVIEW_UNMARKED);
            break;
        case REVIEW_KEY_INFO:
            session->details = !session->details;
            result = render(session);
            break;
        case REVIEW_KEY_HELP:
            session->help = !session->help;
            result = render(session);
            break;
        case REVIEW_KEY_QUIT:
            done = true;
            break;
        }
    }
    int saved_errno = errno;
    if (review_terminal_leave(&session->terminal) != 0 && result == 0) {
        result = -1;
        saved_errno = errno;
    }
    errno = saved_errno;
    return result;
}

int photoc_command_review_with_output(const char *directory, bool recursive,
                                      bool sort_date, const char *show,
                                      const char *images, const char *state_path,
                                      const photoc_output *output)
{
    if (directory == NULL || show == NULL || images == NULL) {
        errno = EINVAL;
        return photoc_error_report("review", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_INTERNAL, NULL,
                                   "invalid review arguments", errno);
    }
    struct stat directory_info;
    if (stat(directory, &directory_info) != 0)
        return photoc_error_report("review", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_IO, NULL,
                                   "unable to inspect review directory", errno);
    if (!S_ISDIR(directory_info.st_mode))
        return photoc_error_report("review", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_USAGE, NULL,
                                   "expected a directory", 0);
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
        return photoc_error_report("review", PHOTOC_ERR_NOTE_NONE,
                                   PHOTOC_ERR_UNSUPPORTED, NULL,
                                   "interactive review requires a terminal", 0);

    review_session session = {.state.directory_fd = -1};
    const char *no_color = getenv("NO_COLOR");
    const char *term = getenv("TERM");
    session.color_actions = (no_color == NULL || no_color[0] == '\0') &&
                            (term == NULL || strcmp(term, "dumb") != 0);
    session.show_images = strcmp(images, "iterm") == 0 ||
        (strcmp(images, "auto") == 0 &&
         review_iterm_auto_supported(getenv("TERM_PROGRAM"),
                                     getenv("ITERM_SESSION_ID"),
                                     getenv("TERM"),
                                     getenv("TMUX"), getenv("STY")));
    int result = PHOTOC_EXIT_FAILURE;
    if (review_model_load(&session.model, directory, recursive,
                          sort_date ? PHOTOC_REVIEW_SORT_DATE :
                                      PHOTOC_REVIEW_SORT_NAME,
                          parse_show(show)) != 0) {
        photoc_error_report("review", PHOTOC_ERR_NOTE_NONE,
                            PHOTOC_ERR_IO, NULL,
                            "unable to discover JPEG photos", errno);
        goto cleanup;
    }
    if (photoc_progress_interrupted())
        goto cleanup;
    if (review_state_open(&session.state, directory, state_path) != 0) {
        int error = errno;
        const char *detail = error == EXDEV ?
            "review state belongs to another directory" :
            error == ENOTSUP ? "unsupported review state version" :
            error == EINVAL ? "malformed or unrelated review state file" :
                              "unable to open review state";
        photoc_error_report("review", PHOTOC_ERR_NOTE_NONE,
                            PHOTOC_ERR_IO, NULL, detail,
                            error == EXDEV || error == ENOTSUP || error == EINVAL ?
                            0 : error);
        goto cleanup;
    }
    if (review_state_apply(&session.state, &session.model) != 0) {
        photoc_error_report("review", PHOTOC_ERR_NOTE_NONE,
                            PHOTOC_ERR_INTERNAL, NULL,
                            "unable to restore review selections", errno);
        goto cleanup;
    }
    if (session.model.count == 0) {
        photoc_output_info(output, "No JPEG photos to review.\n");
        result = PHOTOC_EXIT_SUCCESS;
        goto cleanup;
    }
    if (session.model.visible_count == 0) {
        photoc_output_info(output, "No %s photos to review.\n",
                           show_name(session.model.show));
        photoc_output_info(output, "Picked: %zu  Rejected: %zu  Unmarked: %zu\n",
                           session.model.picked, session.model.rejected,
                           session.model.unmarked);
        result = PHOTOC_EXIT_SUCCESS;
        goto cleanup;
    }
    session.scores = calloc(session.model.count, sizeof(*session.scores));
    if (session.scores == NULL) {
        photoc_error_report("review", PHOTOC_ERR_NOTE_NONE,
                            PHOTOC_ERR_INTERNAL, NULL,
                            "unable to allocate review cache", ENOMEM);
        goto cleanup;
    }
    if (run_terminal(&session) != 0) {
        if (!photoc_progress_interrupted())
            photoc_error_report("review", PHOTOC_ERR_NOTE_NONE,
                                PHOTOC_ERR_IO, NULL,
                                review_terminal_hung_up() ? "terminal disconnected" :
                                                            "terminal I/O failure",
                                errno);
        goto cleanup;
    }
    if (session.save_failed) {
        photoc_output_info(output, "Review ended with unsaved selections.\n");
        photoc_output_info(output,
                           "%zu photos: %zu picked, %zu rejected, %zu unmarked\n",
                           session.model.count, session.model.picked,
                           session.model.rejected, session.model.unmarked);
        photoc_error_report("review", PHOTOC_ERR_NOTE_NONE,
                            PHOTOC_ERR_IO, NULL,
                            "one or more selections were not saved", 0);
        goto cleanup;
    }
    photoc_output_info(output, "Review %s.\n",
                       session.state.exists ? "saved" : "finished without changes");
    photoc_output_info(output, "%zu photos: %zu picked, %zu rejected, %zu unmarked\n",
                       session.model.count, session.model.picked,
                       session.model.rejected, session.model.unmarked);
    result = PHOTOC_EXIT_SUCCESS;
cleanup:
    free(session.scores);
    review_state_cleanup(&session.state);
    review_model_cleanup(&session.model);
    return result;
}
