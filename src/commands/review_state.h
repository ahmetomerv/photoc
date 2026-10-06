#ifndef PHOTOC_REVIEW_STATE_H
#define PHOTOC_REVIEW_STATE_H

#include "review_model.h"

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    char *path; /* Owned relative filesystem bytes. */
    photoc_review_status status;
} review_state_entry;

typedef struct {
    char *root;                  /* Owned canonical review root. */
    char *directory;             /* Owned canonical state parent directory. */
    char *filename;              /* Owned state-file leaf name. */
    review_state_entry *entries; /* Owned sorted array and paths. */
    size_t count;
    char *snapshot; /* Owned bytes of the last loaded/written state file. */
    size_t snapshot_length;
    bool exists;
    int directory_fd;
} review_state;

/* out must be zero-initialized or cleaned up. root and state_path are borrowed;
   NULL state_path selects <canonical root>/.photoc-review.json. A missing state
   is valid and creates no file. Existing malformed/unrelated/symlink files are
   refused. On failure out is cleaned and reusable. Returns 0 or -1 with
   errno set. */
int review_state_open(review_state *out, const char *root,
                      const char *state_path);

/* Restore discovered marks and recompute whole-collection counts/filter. */
int review_state_apply(const review_state *state, review_model *model);

/* Persist a proposed mark before calling review_model_mark_current. On failure
   the state object and model remain unchanged. Only picked/rejected are stored;
   unmarked removes an entry. Stale entries remain present. */
int review_state_save_mark(review_state *state, const char *relative_path,
                           photoc_review_status status);

void review_state_cleanup(review_state *state);

#endif
