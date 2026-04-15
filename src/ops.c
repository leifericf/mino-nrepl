/*
 * ops.c -- nREPL operation handlers.
 *
 * Supported ops: clone, close, describe, eval, completions,
 *                load-file, ls-sessions.
 */

#include "ops.h"
#include "bencode.h"
#include "session.h"
#include "mino.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* -------------------------------------------------------------------- */
/* Helpers                                                              */
/* -------------------------------------------------------------------- */

/* Send a bencode value on a file descriptor. */
static void send_msg(int fd, bc_val_t *msg)
{
    char  *buf = NULL;
    size_t len = 0;
    if (bc_encode(msg, &buf, &len) == 0) {
        size_t written = 0;
        while (written < len) {
            ssize_t n = write(fd, buf + written, len - written);
            if (n <= 0) break;
            written += (size_t)n;
        }
        free(buf);
    }
}

/* Extract a string field from a dict, or return NULL. */
static const char *dict_str(bc_val_t *msg, const char *key)
{
    bc_val_t *v = bc_dict_get(msg, key);
    if (v && v->type == BC_STRING) return v->as.str.data;
    return NULL;
}

/* Build a status list from a NULL-terminated array of strings. */
static bc_val_t *status_list(const char **items)
{
    size_t     n = 0;
    bc_val_t **vals;
    size_t     i;
    while (items[n]) n++;
    vals = malloc(n * sizeof(bc_val_t *));
    if (!vals) return bc_list(NULL, 0);
    for (i = 0; i < n; i++) vals[i] = bc_cstring(items[i]);
    {
        bc_val_t *list = bc_list(vals, n);
        free(vals);
        return list;
    }
}

/* Create a base response dict with id and session copied from the request. */
static bc_val_t *base_response(bc_val_t *msg)
{
    bc_val_t   *resp = bc_dict_new();
    const char *id   = dict_str(msg, "id");
    const char *sess = dict_str(msg, "session");
    if (id)   bc_dict_set(resp, "id",      bc_cstring(id));
    if (sess) bc_dict_set(resp, "session",  bc_cstring(sess));
    return resp;
}

/* -------------------------------------------------------------------- */
/* Op: clone                                                            */
/* -------------------------------------------------------------------- */

static void op_clone(bc_val_t *msg, int fd)
{
    nrepl_session_t *s    = session_create();
    bc_val_t        *resp = base_response(msg);
    const char *done[] = {"done", NULL};

    if (s) {
        bc_dict_set(resp, "new-session", bc_cstring(s->id));
        bc_dict_set(resp, "status",      status_list(done));
    } else {
        const char *err[] = {"done", "error", NULL};
        bc_dict_set(resp, "status", status_list(err));
    }
    send_msg(fd, resp);
    bc_free(resp);
}

/* -------------------------------------------------------------------- */
/* Op: close                                                            */
/* -------------------------------------------------------------------- */

static void op_close(bc_val_t *msg, int fd)
{
    const char *sess = dict_str(msg, "session");
    bc_val_t   *resp = base_response(msg);
    const char *done[] = {"done", NULL};

    if (sess) session_close(sess);
    bc_dict_set(resp, "status", status_list(done));
    send_msg(fd, resp);
    bc_free(resp);
}

/* -------------------------------------------------------------------- */
/* Op: describe                                                         */
/* -------------------------------------------------------------------- */

static void op_describe(bc_val_t *msg, int fd)
{
    bc_val_t   *resp = base_response(msg);
    bc_val_t   *ops  = bc_dict_new();
    bc_val_t   *vers = bc_dict_new();
    const char *done[] = {"done", NULL};

    /* Advertise supported ops. */
    bc_dict_set(ops, "clone",       bc_dict_new());
    bc_dict_set(ops, "close",       bc_dict_new());
    bc_dict_set(ops, "describe",    bc_dict_new());
    bc_dict_set(ops, "eval",        bc_dict_new());
    bc_dict_set(ops, "completions", bc_dict_new());
    bc_dict_set(ops, "load-file",   bc_dict_new());
    bc_dict_set(ops, "ls-sessions", bc_dict_new());

    bc_dict_set(vers, "mino-nrepl", bc_cstring("0.1.0"));

    bc_dict_set(resp, "ops",      ops);
    bc_dict_set(resp, "versions", vers);
    bc_dict_set(resp, "status",   status_list(done));

    send_msg(fd, resp);
    bc_free(resp);
}

/* -------------------------------------------------------------------- */
/* Op: eval                                                             */
/* -------------------------------------------------------------------- */

static void op_eval(bc_val_t *msg, int fd)
{
    const char      *code = dict_str(msg, "code");
    const char      *sess = dict_str(msg, "session");
    nrepl_session_t *s;
    mino_val_t      *result;

    if (!code || !sess) {
        bc_val_t   *resp = base_response(msg);
        const char *err[] = {"done", "error", NULL};
        bc_dict_set(resp, "err",    bc_cstring("eval requires code and session"));
        bc_dict_set(resp, "status", status_list(err));
        send_msg(fd, resp);
        bc_free(resp);
        return;
    }

    s = session_find(sess);
    if (!s) {
        bc_val_t   *resp = base_response(msg);
        const char *err[] = {"done", "error", NULL};
        bc_dict_set(resp, "err",    bc_cstring("unknown session"));
        bc_dict_set(resp, "status", status_list(err));
        send_msg(fd, resp);
        bc_free(resp);
        return;
    }

    session_reset_output(s);
    session_set_current(s);
    result = mino_eval_string(session_state(), code, s->env);
    session_set_current(NULL);

    /* Send captured stdout as "out" message if any. */
    if (s->out_len > 0) {
        bc_val_t *out_resp = base_response(msg);
        bc_dict_set(out_resp, "out", bc_string(s->out_buf, s->out_len));
        send_msg(fd, out_resp);
        bc_free(out_resp);
    }

    if (result) {
        /* Success: send value. */
        char       *repr = session_print_value(result);
        bc_val_t   *resp = base_response(msg);
        const char *done[] = {"done", NULL};

        bc_dict_set(resp, "value", bc_cstring(repr ? repr : "nil"));
        bc_dict_set(resp, "ns",    bc_cstring("user"));
        bc_dict_set(resp, "status", status_list(done));
        send_msg(fd, resp);
        bc_free(resp);
        free(repr);
    } else {
        /* Error. */
        const char *errmsg = mino_last_error(session_state());
        bc_val_t   *resp   = base_response(msg);
        const char *err[]  = {"done", "error", NULL};

        bc_dict_set(resp, "err",    bc_cstring(errmsg ? errmsg : "unknown error"));
        bc_dict_set(resp, "ex",     bc_cstring(errmsg ? errmsg : "unknown error"));
        bc_dict_set(resp, "status", status_list(err));
        send_msg(fd, resp);
        bc_free(resp);
    }
}

/* -------------------------------------------------------------------- */
/* Op: completions                                                      */
/* -------------------------------------------------------------------- */

static void op_completions(bc_val_t *msg, int fd)
{
    const char      *prefix = dict_str(msg, "prefix");
    const char      *sess   = dict_str(msg, "session");
    nrepl_session_t *s;
    mino_val_t      *all_syms;
    bc_val_t        *resp;
    const char      *done[] = {"done", NULL};
    size_t           match_count = 0;
    bc_val_t       **matches = NULL;
    size_t           matches_cap = 0;

    if (!prefix) prefix = "";

    s = sess ? session_find(sess) : NULL;
    if (!s) {
        /* Use first available session as fallback. */
        s = session_list();
    }
    if (!s) {
        resp = base_response(msg);
        bc_dict_set(resp, "completions", bc_list(NULL, 0));
        bc_dict_set(resp, "status",      status_list(done));
        send_msg(fd, resp);
        bc_free(resp);
        return;
    }

    /* Get all symbols via apropos with empty string. */
    all_syms = mino_eval_string(session_state(), "(apropos \"\")", s->env);

    /* Walk the symbol list and filter by prefix. */
    {
        mino_val_t *cur = all_syms;
        size_t      plen = strlen(prefix);

        while (cur && !mino_is_nil(cur) && mino_is_cons(cur)) {
            mino_val_t *sym = mino_car(cur);
            const char *name;
            size_t      nlen;

            /* apropos returns a list of symbols; get the symbol name. */
            if (mino_to_string(sym, &name, &nlen)) {
                /* mino_to_string works on strings; symbols need print. */
                if (plen == 0 || strncmp(name, prefix, plen) == 0) {
                    bc_val_t *entry = bc_dict_new();
                    bc_dict_set(entry, "candidate", bc_cstring(name));
                    if (match_count >= matches_cap) {
                        matches_cap = matches_cap ? matches_cap * 2 : 32;
                        matches = realloc(matches, matches_cap * sizeof(bc_val_t *));
                    }
                    matches[match_count++] = entry;
                }
            } else {
                /* It's a symbol — print it to get the name. */
                char *repr = session_print_value(sym);
                if (repr && (plen == 0 || strncmp(repr, prefix, plen) == 0)) {
                    bc_val_t *entry = bc_dict_new();
                    bc_dict_set(entry, "candidate", bc_cstring(repr));
                    if (match_count >= matches_cap) {
                        matches_cap = matches_cap ? matches_cap * 2 : 32;
                        matches = realloc(matches, matches_cap * sizeof(bc_val_t *));
                    }
                    matches[match_count++] = entry;
                }
                free(repr);
            }
            cur = mino_cdr(cur);
        }
    }

    resp = base_response(msg);
    bc_dict_set(resp, "completions", bc_list(matches, match_count));
    bc_dict_set(resp, "status",      status_list(done));
    send_msg(fd, resp);
    bc_free(resp);
    free(matches);
}

/* -------------------------------------------------------------------- */
/* Op: load-file                                                        */
/* -------------------------------------------------------------------- */

static void op_load_file(bc_val_t *msg, int fd)
{
    const char *file = dict_str(msg, "file");
    if (!file) {
        bc_val_t   *resp = base_response(msg);
        const char *err[] = {"done", "error", NULL};
        bc_dict_set(resp, "err",    bc_cstring("load-file requires file content"));
        bc_dict_set(resp, "status", status_list(err));
        send_msg(fd, resp);
        bc_free(resp);
        return;
    }
    /* Reuse eval by setting the code field. */
    bc_dict_set(msg, "code", bc_cstring(file));
    op_eval(msg, fd);
}

/* -------------------------------------------------------------------- */
/* Op: ls-sessions                                                      */
/* -------------------------------------------------------------------- */

static void op_ls_sessions(bc_val_t *msg, int fd)
{
    nrepl_session_t *s;
    bc_val_t       **ids = NULL;
    size_t           n   = 0;
    size_t           cap = 0;
    bc_val_t        *resp;
    const char      *done[] = {"done", NULL};

    for (s = session_list(); s; s = s->next) {
        if (n >= cap) {
            cap = cap ? cap * 2 : 8;
            ids = realloc(ids, cap * sizeof(bc_val_t *));
        }
        ids[n++] = bc_cstring(s->id);
    }

    resp = base_response(msg);
    bc_dict_set(resp, "sessions", bc_list(ids, n));
    bc_dict_set(resp, "status",   status_list(done));
    send_msg(fd, resp);
    bc_free(resp);
    free(ids);
}

/* -------------------------------------------------------------------- */
/* Dispatch                                                             */
/* -------------------------------------------------------------------- */

typedef void (*op_fn)(bc_val_t *msg, int fd);

static const struct {
    const char *name;
    op_fn       handler;
} op_table[] = {
    { "clone",       op_clone },
    { "close",       op_close },
    { "describe",    op_describe },
    { "eval",        op_eval },
    { "completions", op_completions },
    { "load-file",   op_load_file },
    { "ls-sessions", op_ls_sessions },
    { NULL,          NULL }
};

void ops_dispatch(bc_val_t *msg, int client_fd)
{
    const char *op = dict_str(msg, "op");
    int         i;

    if (!op) {
        bc_val_t   *resp = base_response(msg);
        const char *err[] = {"done", "error", "unknown-op", NULL};
        bc_dict_set(resp, "status", status_list(err));
        send_msg(client_fd, resp);
        bc_free(resp);
        return;
    }

    for (i = 0; op_table[i].name; i++) {
        if (strcmp(op, op_table[i].name) == 0) {
            op_table[i].handler(msg, client_fd);
            return;
        }
    }

    /* Unknown op. */
    {
        bc_val_t   *resp = base_response(msg);
        const char *err[] = {"done", "error", "unknown-op", NULL};
        bc_dict_set(resp, "status", status_list(err));
        send_msg(client_fd, resp);
        bc_free(resp);
    }
}
