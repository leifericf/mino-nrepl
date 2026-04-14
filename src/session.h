/*
 * session.h -- nREPL session management.
 *
 * Each session owns an isolated mino_env_t. Output from println/prn
 * during eval is captured to a per-session buffer.
 */

#ifndef SESSION_H
#define SESSION_H

#include "mino.h"
#include <stddef.h>

typedef struct nrepl_session {
    char                  id[37];    /* UUID v4: 8-4-4-4-12 + NUL */
    mino_env_t           *env;
    char                 *out_buf;   /* captured stdout during eval */
    size_t                out_len;
    size_t                out_cap;
    struct nrepl_session *next;
} nrepl_session_t;

/* Create a fresh session with its own env. Returns NULL on failure. */
nrepl_session_t *session_create(void);

/* Look up a session by its UUID string. Returns NULL if not found. */
nrepl_session_t *session_find(const char *id);

/* Close and free a session. */
void session_close(const char *id);

/* Return the head of the session linked list (for enumeration). */
nrepl_session_t *session_list(void);

/* Clear the output buffer before an eval. */
void session_reset_output(nrepl_session_t *s);

/*
 * Set the active session for output capture. Must be called before
 * mino_eval_string and cleared (set to NULL) after. Single-threaded only.
 */
void session_set_current(nrepl_session_t *s);

/*
 * Capture a mino value's printed representation to a NUL-terminated
 * string. Caller must free the returned pointer. Returns NULL on failure.
 */
char *session_print_value(const mino_val_t *val);

#endif /* SESSION_H */
