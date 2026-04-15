/*
 * session.c -- nREPL session management.
 *
 * Each session owns an independent mino_state_t and mino_env_t.
 * Sessions share nothing and can (in principle) evaluate concurrently.
 */

#include "session.h"
#include "mino.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

/* -------------------------------------------------------------------- */
/* Session list                                                         */
/* -------------------------------------------------------------------- */

static nrepl_session_t *sessions        = NULL;

/* -------------------------------------------------------------------- */
/* Active session for output capture (single-threaded)                  */
/* -------------------------------------------------------------------- */

static nrepl_session_t *current_session = NULL;

void session_set_current(nrepl_session_t *s) { current_session = s; }

/* Append text to the current session's output buffer. */
static void capture_append(const char *text, size_t len)
{
    nrepl_session_t *s = current_session;
    if (!s) return;
    if (s->out_len + len >= s->out_cap) {
        size_t newcap = s->out_cap ? s->out_cap : 256;
        char  *nb;
        while (newcap <= s->out_len + len) newcap *= 2;
        nb = realloc(s->out_buf, newcap);
        if (!nb) return;
        s->out_buf = nb;
        s->out_cap = newcap;
    }
    memcpy(s->out_buf + s->out_len, text, len);
    s->out_len += len;
    s->out_buf[s->out_len] = '\0';
}

/* -------------------------------------------------------------------- */
/* Custom println / prn that capture to session buffer                  */
/* -------------------------------------------------------------------- */

/* Helper: print a mino value to a malloc'd string via tmpfile. */
static char *val_to_str(mino_state_t *S, const mino_val_t *v)
{
    FILE *tmp = tmpfile();
    long  len;
    char *buf;

    if (!tmp) return NULL;
    mino_print_to(S, tmp, v);
    len = ftell(tmp);
    if (len <= 0) { fclose(tmp); return NULL; }
    buf = malloc((size_t)len + 1);
    if (!buf) { fclose(tmp); return NULL; }
    rewind(tmp);
    if (fread(buf, 1, (size_t)len, tmp) != (size_t)len) {
        free(buf);
        fclose(tmp);
        return NULL;
    }
    buf[len] = '\0';
    fclose(tmp);
    return buf;
}

/* (println & args) — print each arg separated by spaces, with newline. */
static mino_val_t *capture_println(mino_state_t *S, mino_val_t *args,
                                   mino_env_t *env)
{
    int first = 1;
    (void)env;
    while (args && !mino_is_nil(args) && mino_is_cons(args)) {
        mino_val_t *v = mino_car(args);
        const char *s;
        size_t      slen;
        if (!first) capture_append(" ", 1);
        /* For strings, print the raw content (not quoted). */
        if (mino_to_string(v, &s, &slen)) {
            capture_append(s, slen);
        } else {
            char *repr = val_to_str(S, v);
            if (repr) {
                capture_append(repr, strlen(repr));
                free(repr);
            }
        }
        first = 0;
        args = mino_cdr(args);
    }
    capture_append("\n", 1);
    return mino_nil(S);
}

/* (prn & args) — print each arg's pr-str separated by spaces, with newline. */
static mino_val_t *capture_prn(mino_state_t *S, mino_val_t *args,
                               mino_env_t *env)
{
    int first = 1;
    (void)env;
    while (args && !mino_is_nil(args) && mino_is_cons(args)) {
        mino_val_t *v = mino_car(args);
        char *repr = val_to_str(S, v);
        if (!first) capture_append(" ", 1);
        if (repr) {
            capture_append(repr, strlen(repr));
            free(repr);
        }
        first = 0;
        args = mino_cdr(args);
    }
    capture_append("\n", 1);
    return mino_nil(S);
}

/* -------------------------------------------------------------------- */
/* UUID v4 generation                                                   */
/* -------------------------------------------------------------------- */

static void generate_uuid(char out[37])
{
    unsigned char bytes[16];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0 || read(fd, bytes, 16) != 16) {
        /* Fallback: use address entropy if /dev/urandom fails. */
        size_t i;
        unsigned long seed = (unsigned long)(size_t)&out ^ (unsigned long)sessions;
        for (i = 0; i < 16; i++) {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            bytes[i] = (unsigned char)(seed >> 33);
        }
    }
    if (fd >= 0) close(fd);

    /* Set version (4) and variant (10xx). */
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    snprintf(out, 37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2],  bytes[3],
        bytes[4], bytes[5], bytes[6],  bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);
}

/* -------------------------------------------------------------------- */
/* Public API                                                           */
/* -------------------------------------------------------------------- */

nrepl_session_t *session_create(void)
{
    nrepl_session_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;

    generate_uuid(s->id);

    /* Each session gets its own isolated runtime state. */
    s->state = mino_state_new();
    if (!s->state) { free(s); return NULL; }

    s->env = mino_new(s->state);
    if (!s->env) { mino_state_free(s->state); free(s); return NULL; }

    /* Replace println/prn with capturing versions. */
    mino_env_set(s->state, s->env, "println",
                 mino_prim(s->state, "println", capture_println));
    mino_env_set(s->state, s->env, "prn",
                 mino_prim(s->state, "prn",     capture_prn));

    s->next  = sessions;
    sessions = s;
    return s;
}

nrepl_session_t *session_find(const char *id)
{
    nrepl_session_t *s;
    for (s = sessions; s; s = s->next) {
        if (strcmp(s->id, id) == 0) return s;
    }
    return NULL;
}

void session_close(const char *id)
{
    nrepl_session_t **pp = &sessions;
    while (*pp) {
        if (strcmp((*pp)->id, id) == 0) {
            nrepl_session_t *s = *pp;
            *pp = s->next;
            mino_env_free(s->state, s->env);
            mino_state_free(s->state);
            free(s->out_buf);
            free(s);
            return;
        }
        pp = &(*pp)->next;
    }
}

nrepl_session_t *session_list(void) { return sessions; }

void session_reset_output(nrepl_session_t *s)
{
    if (s) {
        s->out_len = 0;
        if (s->out_buf) s->out_buf[0] = '\0';
    }
}

char *session_print_value(mino_state_t *S, const mino_val_t *val)
{
    return val_to_str(S, val);
}
