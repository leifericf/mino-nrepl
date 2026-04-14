/*
 * bencode.c -- bencode encoder/decoder.
 */

#include "bencode.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------- */
/* Constructors                                                         */
/* -------------------------------------------------------------------- */

bc_val_t *bc_string(const char *s, size_t len)
{
    bc_val_t *v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->type = BC_STRING;
    v->as.str.data = malloc(len + 1);
    if (!v->as.str.data) { free(v); return NULL; }
    memcpy(v->as.str.data, s, len);
    v->as.str.data[len] = '\0';
    v->as.str.len = len;
    return v;
}

bc_val_t *bc_cstring(const char *s)
{
    return bc_string(s, strlen(s));
}

bc_val_t *bc_int(long long n)
{
    bc_val_t *v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->type = BC_INT;
    v->as.num = n;
    return v;
}

bc_val_t *bc_list(bc_val_t **items, size_t n)
{
    bc_val_t *v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->type = BC_LIST;
    if (n > 0) {
        v->as.list.items = malloc(n * sizeof(bc_val_t *));
        if (!v->as.list.items) { free(v); return NULL; }
        memcpy(v->as.list.items, items, n * sizeof(bc_val_t *));
    }
    v->as.list.len = n;
    return v;
}

bc_val_t *bc_dict_new(void)
{
    bc_val_t *v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->type = BC_DICT;
    return v;
}

void bc_dict_set(bc_val_t *dict, const char *key, bc_val_t *val)
{
    size_t i;
    if (!dict || dict->type != BC_DICT) return;

    /* Replace if key already exists. */
    for (i = 0; i < dict->as.dict.len; i++) {
        if (strcmp(dict->as.dict.keys[i]->as.str.data, key) == 0) {
            bc_free(dict->as.dict.vals[i]);
            dict->as.dict.vals[i] = val;
            return;
        }
    }

    /* Grow arrays. */
    if (dict->as.dict.len >= dict->as.dict.cap) {
        size_t     newcap = dict->as.dict.cap ? dict->as.dict.cap * 2 : 8;
        bc_val_t **nk = realloc(dict->as.dict.keys, newcap * sizeof(bc_val_t *));
        bc_val_t **nv = realloc(dict->as.dict.vals, newcap * sizeof(bc_val_t *));
        if (!nk || !nv) return;
        dict->as.dict.keys = nk;
        dict->as.dict.vals = nv;
        dict->as.dict.cap  = newcap;
    }
    dict->as.dict.keys[dict->as.dict.len] = bc_cstring(key);
    dict->as.dict.vals[dict->as.dict.len] = val;
    dict->as.dict.len++;
}

bc_val_t *bc_dict_get(const bc_val_t *dict, const char *key)
{
    size_t i;
    if (!dict || dict->type != BC_DICT) return NULL;
    for (i = 0; i < dict->as.dict.len; i++) {
        if (strcmp(dict->as.dict.keys[i]->as.str.data, key) == 0)
            return dict->as.dict.vals[i];
    }
    return NULL;
}

/* -------------------------------------------------------------------- */
/* Decoder                                                              */
/* -------------------------------------------------------------------- */

static bc_val_t *decode_one(const char *buf, size_t len, size_t *consumed);

/* Decode a bencode string: "4:spam" */
static bc_val_t *decode_string(const char *buf, size_t len, size_t *consumed)
{
    size_t     slen = 0;
    size_t     i    = 0;

    /* Parse length prefix. */
    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        slen = slen * 10 + (size_t)(buf[i] - '0');
        i++;
    }
    if (i == 0 || i >= len) { *consumed = 0; return NULL; }
    if (buf[i] != ':')      { *consumed = i; return NULL; } /* malformed */
    i++; /* skip ':' */

    if (i + slen > len) { *consumed = 0; return NULL; } /* incomplete */

    *consumed = i + slen;
    return bc_string(buf + i, slen);
}

/* Decode a bencode integer: "i42e" or "i-3e" */
static bc_val_t *decode_int(const char *buf, size_t len, size_t *consumed)
{
    long long  num  = 0;
    int        neg  = 0;
    size_t     i    = 1; /* skip 'i' */

    if (len < 3) { *consumed = 0; return NULL; } /* need at least "i0e" */

    if (i < len && buf[i] == '-') { neg = 1; i++; }

    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        num = num * 10 + (buf[i] - '0');
        i++;
    }
    if (i >= len)     { *consumed = 0; return NULL; } /* incomplete */
    if (buf[i] != 'e') { *consumed = i; return NULL; } /* malformed */
    i++; /* skip 'e' */

    *consumed = i;
    return bc_int(neg ? -num : num);
}

/* Decode a bencode list: "l...e" */
static bc_val_t *decode_list(const char *buf, size_t len, size_t *consumed)
{
    size_t     i    = 1; /* skip 'l' */
    size_t     cap  = 8;
    size_t     n    = 0;
    bc_val_t **items = malloc(cap * sizeof(bc_val_t *));
    bc_val_t  *v;

    if (!items) { *consumed = 0; return NULL; }

    while (i < len && buf[i] != 'e') {
        size_t ate = 0;
        v = decode_one(buf + i, len - i, &ate);
        if (!v) {
            size_t j;
            for (j = 0; j < n; j++) bc_free(items[j]);
            free(items);
            *consumed = (ate == 0) ? 0 : i + ate;
            return NULL;
        }
        if (n >= cap) {
            cap *= 2;
            items = realloc(items, cap * sizeof(bc_val_t *));
        }
        items[n++] = v;
        i += ate;
    }
    if (i >= len) { /* incomplete — no 'e' found */
        size_t j;
        for (j = 0; j < n; j++) bc_free(items[j]);
        free(items);
        *consumed = 0;
        return NULL;
    }
    i++; /* skip 'e' */

    *consumed = i;
    return bc_list(items, n);
}

/* Decode a bencode dict: "d...e" */
static bc_val_t *decode_dict(const char *buf, size_t len, size_t *consumed)
{
    size_t     i = 1; /* skip 'd' */
    bc_val_t  *dict = bc_dict_new();

    if (!dict) { *consumed = 0; return NULL; }

    while (i < len && buf[i] != 'e') {
        size_t     key_ate = 0, val_ate = 0;
        bc_val_t  *key, *val;

        key = decode_one(buf + i, len - i, &key_ate);
        if (!key || key->type != BC_STRING) {
            if (key) bc_free(key);
            bc_free(dict);
            *consumed = (key_ate == 0) ? 0 : i + key_ate;
            return NULL;
        }
        i += key_ate;

        val = decode_one(buf + i, len - i, &val_ate);
        if (!val) {
            bc_free(key);
            bc_free(dict);
            *consumed = (val_ate == 0) ? 0 : i + val_ate;
            return NULL;
        }
        i += val_ate;

        bc_dict_set(dict, key->as.str.data, val);
        bc_free(key); /* dict_set copies the key string */
    }
    if (i >= len) { /* incomplete */
        bc_free(dict);
        *consumed = 0;
        return NULL;
    }
    i++; /* skip 'e' */

    *consumed = i;
    return dict;
}

static bc_val_t *decode_one(const char *buf, size_t len, size_t *consumed)
{
    if (len == 0) { *consumed = 0; return NULL; }

    if (buf[0] >= '0' && buf[0] <= '9') return decode_string(buf, len, consumed);
    if (buf[0] == 'i')                   return decode_int(buf, len, consumed);
    if (buf[0] == 'l')                   return decode_list(buf, len, consumed);
    if (buf[0] == 'd')                   return decode_dict(buf, len, consumed);

    /* Unknown byte — malformed. */
    *consumed = 1;
    return NULL;
}

bc_val_t *bc_decode(const char *buf, size_t len, size_t *consumed)
{
    return decode_one(buf, len, consumed);
}

/* -------------------------------------------------------------------- */
/* Encoder                                                              */
/* -------------------------------------------------------------------- */

/* Growable buffer for encoding. */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} encbuf_t;

static int enc_grow(encbuf_t *b, size_t need)
{
    if (b->len + need <= b->cap) return 0;
    {
        size_t newcap = b->cap ? b->cap : 64;
        char  *nd;
        while (newcap < b->len + need) newcap *= 2;
        nd = realloc(b->data, newcap);
        if (!nd) return -1;
        b->data = nd;
        b->cap  = newcap;
    }
    return 0;
}

static int enc_append(encbuf_t *b, const char *s, size_t n)
{
    if (enc_grow(b, n) < 0) return -1;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    return 0;
}

static int enc_val(encbuf_t *b, const bc_val_t *val);

static int enc_string(encbuf_t *b, const char *s, size_t slen)
{
    char prefix[32];
    int  plen = snprintf(prefix, sizeof(prefix), "%zu:", slen);
    if (enc_append(b, prefix, (size_t)plen) < 0) return -1;
    return enc_append(b, s, slen);
}

static int enc_int(encbuf_t *b, long long n)
{
    char tmp[32];
    int  tlen = snprintf(tmp, sizeof(tmp), "i%llde", n);
    return enc_append(b, tmp, (size_t)tlen);
}

static int enc_list(encbuf_t *b, const bc_val_t *val)
{
    size_t i;
    if (enc_append(b, "l", 1) < 0) return -1;
    for (i = 0; i < val->as.list.len; i++) {
        if (enc_val(b, val->as.list.items[i]) < 0) return -1;
    }
    return enc_append(b, "e", 1);
}

static int enc_dict(encbuf_t *b, const bc_val_t *val)
{
    size_t i;
    if (enc_append(b, "d", 1) < 0) return -1;
    for (i = 0; i < val->as.dict.len; i++) {
        bc_val_t *k = val->as.dict.keys[i];
        if (enc_string(b, k->as.str.data, k->as.str.len) < 0) return -1;
        if (enc_val(b, val->as.dict.vals[i]) < 0) return -1;
    }
    return enc_append(b, "e", 1);
}

static int enc_val(encbuf_t *b, const bc_val_t *val)
{
    switch (val->type) {
    case BC_STRING: return enc_string(b, val->as.str.data, val->as.str.len);
    case BC_INT:    return enc_int(b, val->as.num);
    case BC_LIST:   return enc_list(b, val);
    case BC_DICT:   return enc_dict(b, val);
    }
    return -1;
}

int bc_encode(const bc_val_t *val, char **buf, size_t *len)
{
    encbuf_t b = {NULL, 0, 0};
    if (enc_val(&b, val) < 0) {
        free(b.data);
        return -1;
    }
    *buf = b.data;
    *len = b.len;
    return 0;
}

/* -------------------------------------------------------------------- */
/* Cleanup                                                              */
/* -------------------------------------------------------------------- */

void bc_free(bc_val_t *val)
{
    size_t i;
    if (!val) return;
    switch (val->type) {
    case BC_STRING:
        free(val->as.str.data);
        break;
    case BC_INT:
        break;
    case BC_LIST:
        for (i = 0; i < val->as.list.len; i++)
            bc_free(val->as.list.items[i]);
        free(val->as.list.items);
        break;
    case BC_DICT:
        for (i = 0; i < val->as.dict.len; i++) {
            bc_free(val->as.dict.keys[i]);
            bc_free(val->as.dict.vals[i]);
        }
        free(val->as.dict.keys);
        free(val->as.dict.vals);
        break;
    }
    free(val);
}
