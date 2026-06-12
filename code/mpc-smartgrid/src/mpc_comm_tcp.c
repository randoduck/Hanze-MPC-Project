#include <time.h>
/*
 * mpc_comm_tcp.c  --  TCP communication layer for MPC
 *
 * Each party i listens on port MPC_BASE_PORT + i.
 * Connections from lower-ID parties are accepted; connections to lower-ID
 * parties are initiated with a retry loop (so startup order doesn't matter).
 *
 * A background thread runs the accept() loop so the main thread can
 * simultaneously attempt outgoing connections without deadlocking.
 *
 * Thread safety: _peer_fd[] is written only by the acceptor thread and
 * read by the main thread only after comm_init returns. comm_send and
 * comm_recv are each called from the main thread only during the protocol.
 */

#include "mpc_comm.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>


static int _my_id    = -1;
static int _n        =  0;
static int _peer_fd[MPC_MAX_PARTIES];   /* fd to talk to peer i; -1 = not connected */

static pthread_mutex_t _fd_mutex = PTHREAD_MUTEX_INITIALIZER;



typedef struct {
    int listen_fd;   /* already bound + listening before the thread starts */
    int n_to_accept; /* how many incoming connections to expect */
} acceptor_arg_t;

static void *acceptor_thread(void *arg) {
    acceptor_arg_t *a   = (acceptor_arg_t *)arg;
    int             lfd = a->listen_fd;
    int             n   = a->n_to_accept;
    free(a);

    for (int i = 0; i < n; i++) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) {
            perror("[comm] accept");
            continue;
        }

        /* Connecting party sends its id as a 4-byte int (network byte order) */
        int peer_id_net;
        ssize_t got = recv(cfd, &peer_id_net, sizeof(peer_id_net), MSG_WAITALL);
        if (got != sizeof(peer_id_net)) {
            fprintf(stderr, "[comm] handshake recv failed\n");
            close(cfd);
            continue;
        }
        int peer_id = (int)ntohl((uint32_t)peer_id_net);

        /* Bounds-check before indexing _peer_fd[]: a malformed or hostile
         * handshake must not write outside the array. */
        if (peer_id < 0 || peer_id >= _n) {
            fprintf(stderr, "[comm] rejecting out-of-range peer id %d\n", peer_id);
            close(cfd);
            continue;
        }

        pthread_mutex_lock(&_fd_mutex);
        _peer_fd[peer_id] = cfd;
        pthread_mutex_unlock(&_fd_mutex);

        printf("[comm] accepted connection from party %d\n", peer_id);
    }

    close(lfd);
    return NULL;
}


int comm_init(int my_id, int n, const char **party_ips) {
    _my_id = my_id;
    _n     = n;

    for (int i = 0; i < MPC_MAX_PARTIES; i++) _peer_fd[i] = -1;

    /* ---- Step 1: bind and listen on our own port ---- */
    int lfd        = -1;
    int n_incoming = n - my_id - 1;   /* parties with higher id connect to us */

    if (n_incoming > 0) {
        lfd = socket(AF_INET, SOCK_STREAM, 0);
        if (lfd < 0) { perror("[comm] socket"); return -1; }

        int opt = 1;
        setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons((uint16_t)(MPC_BASE_PORT + my_id));
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("[comm] bind"); close(lfd); return -1;
        }
        if (listen(lfd, n_incoming) < 0) {
            perror("[comm] listen"); close(lfd); return -1;
        }

        printf("[comm] party %d listening on port %d\n",
               my_id, MPC_BASE_PORT + my_id);

        /* ---- Step 2: spawn acceptor thread ---- */
        acceptor_arg_t *arg = malloc(sizeof(acceptor_arg_t));
        arg->listen_fd   = lfd;
        arg->n_to_accept = n_incoming;

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&tid, &attr, acceptor_thread, arg);
        pthread_attr_destroy(&attr);
    }

    /* ---- Step 3: connect to all parties with lower id ---- */
    for (int j = 0; j < my_id; j++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { perror("[comm] socket"); return -1; }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port   = htons((uint16_t)(MPC_BASE_PORT + j));
        if (inet_pton(AF_INET, party_ips[j], &addr.sin_addr) != 1) {
            fprintf(stderr, "[comm] invalid IP for party %d: %s\n", j, party_ips[j]);
            return -1;
        }

        /* Retry until the peer's listener is ready (startup timing) */
        int attempts = 0;
        while (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            if (++attempts % 10 == 0) {
                printf("[comm] still trying to reach party %d at %s...\n",
                       j, party_ips[j]);
            }
            struct timespec _ts500 = {0, 500000000L}; nanosleep(&_ts500, NULL);   /* 500 ms between retries */
        }

        /* Handshake: send our id so the accepting side can identify us */
        uint32_t id_net = htonl((uint32_t)my_id);
        send(fd, &id_net, sizeof(id_net), 0);

        pthread_mutex_lock(&_fd_mutex);
        _peer_fd[j] = fd;
        pthread_mutex_unlock(&_fd_mutex);

        printf("[comm] connected to party %d at %s\n", j, party_ips[j]);
    }

    /* ---- Step 4: wait until all incoming connections are accepted ---- */
    for (int j = my_id + 1; j < n; j++) {
        while (1) {
            pthread_mutex_lock(&_fd_mutex);
            int fd = _peer_fd[j];
            pthread_mutex_unlock(&_fd_mutex);
            if (fd >= 0) break;
            struct timespec _ts100 = {0, 100000000L}; nanosleep(&_ts100, NULL);   /* poll every 100 ms */
        }
    }

    printf("[comm] all %d parties connected\n", n);
    return 0;
}



int comm_send(int to, const void *buf, size_t len) {
    int fd = _peer_fd[to];
    if (fd < 0) {
        fprintf(stderr, "[comm] no connection to party %d\n", to);
        return -1;
    }

    const char *ptr  = (const char *)buf;
    size_t      sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, ptr + sent, len - sent, 0);
        if (n <= 0) {
            perror("[comm] send");
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}


int comm_recv(int from, void *buf, size_t len, int timeout_ms) {
    int fd = _peer_fd[from];
    if (fd < 0) {
        fprintf(stderr, "[comm] no connection from party %d\n", from);
        return -1;
    }

    char   *ptr      = (char *)buf;
    size_t  received = 0;

    while (received < len) {
        struct pollfd pfd;
        pfd.fd      = fd;
        pfd.events  = POLLIN;
        pfd.revents = 0;

        int ready = poll(&pfd, 1, timeout_ms);
        if (ready == 0) {
            fprintf(stderr, "[comm] recv timeout waiting for party %d\n", from);
            return -1;
        }
        if (ready < 0) {
            perror("[comm] poll");
            return -1;
        }

        ssize_t n = recv(fd, ptr + received, len - received, 0);
        if (n <= 0) {
            fprintf(stderr, "[comm] connection to party %d closed\n", from);
            return -1;
        }
        received += (size_t)n;
    }
    return 0;
}



void comm_close(void) {
    for (int i = 0; i < _n; i++) {
        if (_peer_fd[i] >= 0) {
            close(_peer_fd[i]);
            _peer_fd[i] = -1;
        }
    }
    _my_id = -1;
    _n     =  0;
}
