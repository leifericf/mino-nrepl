/*
 * bencode.h -- bencode encoder/decoder for nREPL wire protocol.
 *
 * Bencode has four types:
 *   string  "4:spam"          length-prefixed bytes
 *   int     "i42e"            signed integer
 *   list    "l4:spami42ee"    ordered sequence
 *   dict    "d3:foo3:bare"    sorted-key map (keys must be strings)
 *
 * The decoder handles partial input gracefully for TCP streaming.
 */

#ifndef BENCODE_H
#define BENCODE_H

#include <stddef.h>

typedef enum {
    BC_STRING,
    BC_INT,
    BC_LIST,
    BC_DICT
} bc_type_t;

typedef struct bc_val {
    bc_type_t type;
    union {
        struct { char *data; size_t len; }           str;
        long long                                    num;
        struct { struct bc_val **items; size_t len; } list;
        struct {
            struct bc_val **keys;   /* all BC_STRING */
            struct bc_val **vals;
            size_t          len;
            size_t          cap;
        } dict;
    } as;
} bc_val_t;

/* --- Constructors --------------------------------------------------- */

bc_val_t *bc_string(const char *s, size_t len);
bc_val_t *bc_cstring(const char *s);          /* NUL-terminated shorthand */
bc_val_t *bc_int(long long n);
bc_val_t *bc_list(bc_val_t **items, size_t n);
bc_val_t *bc_dict_new(void);
void      bc_dict_set(bc_val_t *dict, const char *key, bc_val_t *val);
bc_val_t *bc_dict_get(const bc_val_t *dict, const char *key);

/* --- Codec ---------------------------------------------------------- */

/*
 * Decode one bencode value from `buf` (length `len`).
 * On success: returns the decoded value, *consumed = bytes read.
 * On incomplete input: returns NULL, *consumed = 0.
 * On malformed input: returns NULL, *consumed > 0.
 */
bc_val_t *bc_decode(const char *buf, size_t len, size_t *consumed);

/*
 * Encode a value to a dynamically allocated buffer.
 * Caller must free(*buf) when done.
 * Returns 0 on success, -1 on allocation failure.
 */
int bc_encode(const bc_val_t *val, char **buf, size_t *len);

/* --- Cleanup -------------------------------------------------------- */

void bc_free(bc_val_t *val);

#endif /* BENCODE_H */
