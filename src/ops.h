/*
 * ops.h -- nREPL operation handlers.
 */

#ifndef OPS_H
#define OPS_H

#include "bencode.h"

/*
 * Dispatch an incoming nREPL message to the appropriate op handler.
 * Sends one or more bencode response dicts back on `client_fd`.
 */
void ops_dispatch(bc_val_t *msg, int client_fd);

#endif /* OPS_H */
