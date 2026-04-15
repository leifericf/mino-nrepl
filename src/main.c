/*
 * main.c -- mino-nrepl server.
 *
 * A minimal nREPL server for the mino language. Single-threaded with
 * poll(2) for multiplexing clients. Writes .nrepl-port for editor
 * auto-discovery.
 *
 * Usage: mino-nrepl [--port N] [--bind ADDR]
 */

#include "bencode.h"
#include "ops.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* -------------------------------------------------------------------- */
/* Configuration                                                        */
/* -------------------------------------------------------------------- */

#define MAX_CLIENTS  32
#define READ_BUFSZ   65536

/* -------------------------------------------------------------------- */
/* Per-client read buffer                                               */
/* -------------------------------------------------------------------- */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} clientbuf_t;

static clientbuf_t clients[MAX_CLIENTS];

static void client_init(int idx)
{
    clients[idx].data = NULL;
    clients[idx].len  = 0;
    clients[idx].cap  = 0;
}

static void client_free(int idx)
{
    free(clients[idx].data);
    client_init(idx);
}

static int client_append(int idx, const char *buf, size_t n)
{
    clientbuf_t *c = &clients[idx];
    if (c->len + n >= c->cap) {
        size_t newcap = c->cap ? c->cap : 4096;
        char  *nd;
        while (newcap <= c->len + n) newcap *= 2;
        nd = realloc(c->data, newcap);
        if (!nd) return -1;
        c->data = nd;
        c->cap  = newcap;
    }
    memcpy(c->data + c->len, buf, n);
    c->len += n;
    return 0;
}

/* Shift consumed bytes out of the buffer. */
static void client_consume(int idx, size_t n)
{
    clientbuf_t *c = &clients[idx];
    if (n >= c->len) {
        c->len = 0;
    } else {
        memmove(c->data, c->data + n, c->len - n);
        c->len -= n;
    }
}

/* -------------------------------------------------------------------- */
/* .nrepl-port management                                               */
/* -------------------------------------------------------------------- */

static const char *port_file = ".nrepl-port";

static void remove_port_file(void) { unlink(port_file); }

static void write_port_file(int port)
{
    FILE *f = fopen(port_file, "w");
    if (f) {
        fprintf(f, "%d\n", port);
        fclose(f);
    }
}

/* -------------------------------------------------------------------- */
/* Signal handling                                                      */
/* -------------------------------------------------------------------- */

static volatile sig_atomic_t running = 1;

static void handle_signal(int sig) { (void)sig; running = 0; }

/* -------------------------------------------------------------------- */
/* Argument parsing                                                     */
/* -------------------------------------------------------------------- */

static void parse_args(int argc, char **argv, int *port, const char **bind_addr)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            *port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
            *bind_addr = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: mino-nrepl [--port N] [--bind ADDR]\n");
            printf("  --port N      Listen on port N (default: random)\n");
            printf("  --bind ADDR   Bind to address (default: 127.0.0.1)\n");
            exit(0);
        }
    }
}

/* -------------------------------------------------------------------- */
/* Main                                                                 */
/* -------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    int                port      = 0;
    const char        *bind_addr = "127.0.0.1";
    int                srv_fd;
    struct sockaddr_in addr;
    socklen_t          addrlen = sizeof(addr);
    struct pollfd      fds[1 + MAX_CLIENTS];
    int                nfds = 1;
    int                i;

    parse_args(argc, argv, &port, &bind_addr);

    /* Sessions create their own mino states on demand. */

    /* Set up signal handlers. */
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    /* Create listening socket. */
    srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd < 0) { perror("socket"); return 1; }
    {
        int opt = 1;
        setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = inet_addr(bind_addr);
    addr.sin_port        = htons((unsigned short)port);

    if (bind(srv_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(srv_fd);
        return 1;
    }
    if (listen(srv_fd, 5) < 0) {
        perror("listen");
        close(srv_fd);
        return 1;
    }

    /* Discover actual port if auto-assigned. */
    if (getsockname(srv_fd, (struct sockaddr *)&addr, &addrlen) == 0) {
        port = ntohs(addr.sin_port);
    }

    /* Write .nrepl-port and register cleanup. */
    write_port_file(port);
    atexit(remove_port_file);

    printf("nREPL server started on port %d on host %s - nrepl://%s:%d\n",
           port, bind_addr, bind_addr, port);
    fflush(stdout);

    /* Initialize poll set. */
    memset(fds, 0, sizeof(fds));
    fds[0].fd     = srv_fd;
    fds[0].events = POLLIN;
    for (i = 1; i <= MAX_CLIENTS; i++) {
        fds[i].fd = -1;
        client_init(i - 1);
    }

    /* Main event loop. */
    while (running) {
        int ready = poll(fds, (nfds_t)nfds, 1000);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }
        if (ready == 0) continue;

        /* Check for new connections. */
        if (fds[0].revents & POLLIN) {
            int cli_fd = accept(srv_fd, NULL, NULL);
            if (cli_fd >= 0) {
                if (nfds <= MAX_CLIENTS) {
                    int idx = nfds - 1;
                    fds[nfds].fd     = cli_fd;
                    fds[nfds].events = POLLIN;
                    client_init(idx);
                    nfds++;
                } else {
                    close(cli_fd); /* at capacity */
                }
            }
        }

        /* Process client data. */
        for (i = 1; i < nfds; i++) {
            if (!(fds[i].revents & POLLIN)) continue;

            {
                char    buf[READ_BUFSZ];
                ssize_t n = read(fds[i].fd, buf, sizeof(buf));

                if (n <= 0) {
                    /* Client disconnected. */
                    close(fds[i].fd);
                    client_free(i - 1);

                    /* Compact: move last entry into this slot. */
                    nfds--;
                    if (i < nfds) {
                        fds[i]         = fds[nfds];
                        clients[i - 1] = clients[nfds - 1];
                        client_init(nfds - 1);
                        fds[nfds].fd = -1;
                        i--; /* re-check this slot */
                    } else {
                        fds[i].fd = -1;
                    }
                    continue;
                }

                client_append(i - 1, buf, (size_t)n);

                /* Try to decode and dispatch complete messages. */
                while (clients[i - 1].len > 0) {
                    size_t    consumed = 0;
                    bc_val_t *msg = bc_decode(clients[i - 1].data,
                                              clients[i - 1].len, &consumed);
                    if (!msg) {
                        if (consumed > 0) {
                            /* Malformed data; discard consumed bytes. */
                            client_consume(i - 1, consumed);
                        }
                        break; /* Incomplete or error — wait for more data. */
                    }
                    client_consume(i - 1, consumed);

                    if (msg->type == BC_DICT) {
                        ops_dispatch(msg, fds[i].fd);
                    }
                    bc_free(msg);
                }
            }
        }
    }

    /* Clean shutdown. */
    printf("\nShutting down...\n");
    for (i = 1; i < nfds; i++) {
        if (fds[i].fd >= 0) close(fds[i].fd);
        client_free(i - 1);
    }
    close(srv_fd);
    /* Each session frees its own state when closed.
     * Any remaining sessions are leaked at exit since the process is
     * ending anyway.  A full cleanup would iterate session_list(). */
    /* atexit handler removes .nrepl-port */

    return 0;
}
