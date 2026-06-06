/*
 * test_operator.c  --  Grid Operator (supports 2 to 32 meters)
 *
 * Listens for partial sums from all meters, adds them, prints the aggregate.
 * This is the ONLY device that ever sees the result.
 *
 * Usage:
 *   ./build/test_operator <n> <meter_0_ip> ... <meter_{n-1}_ip>
 *   (meter IPs are informational only -- operator just listens on one port)
 *
 * 2-meter localhost:
 *   ./build/test_operator 2 127.0.0.1 127.0.0.1
 *
 * 5-meter localhost:
 *   ./build/test_operator 5 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1
 *
 * Always start the operator BEFORE the meters.
 */

#include "mpc_field.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define OPERATOR_PORT  5010   /* all meters connect to this single port */
#define MAX_N          32

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr,
            "Usage: %s <n> <meter_0_ip> ... <meter_{n-1}_ip>\n"
            "  n    : total number of meters\n"
            "\n2-meter localhost:  %s 2 127.0.0.1 127.0.0.1\n"
            "5-meter localhost:  %s 5 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1\n",
            argv[0], argv[0], argv[0]);
        return 1;
    }

    int n = atoi(argv[1]);
    if (n < 2 || n > MAX_N) { fprintf(stderr, "n must be 2-%d\n", MAX_N); return 1; }
    if (argc != n + 2)       { fprintf(stderr, "expected %d meter IPs\n", n); return 1; }

    printf("[operator] Grid operator ready. Expecting %d meters.\n\n", n);

    /* Listen on a single port -- each meter connects and sends its id + partial */
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(OPERATOR_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    listen(lfd, n);
    printf("[operator] listening on port %d...\n\n", OPERATOR_PORT);

    field_t partials[MAX_N];
    int     received_from[MAX_N];
    memset(received_from, 0, sizeof(received_from));

    double t_start = now_ms();

    /* Accept one connection per meter -- meters may arrive in any order */
    for (int i = 0; i < n; i++) {
        int cfd = accept(lfd, NULL, NULL);

        /* Each meter sends: [my_id (4 bytes network order)][partial (4 bytes)] */
        uint32_t id_net, p_net;
        recv(cfd, &id_net, sizeof(uint32_t), MSG_WAITALL);
        recv(cfd, &p_net,  sizeof(uint32_t), MSG_WAITALL);
        close(cfd);

        int     meter_id = (int)ntohl(id_net);
        field_t partial  = (field_t)ntohl(p_net);

        if (meter_id < 0 || meter_id >= n) {
            fprintf(stderr, "[operator] received invalid meter id %d -- ignoring\n", meter_id);
            i--;
            continue;
        }

        partials[meter_id]      = partial;
        received_from[meter_id] = 1;

        printf("[operator] received partial from meter %d: %u  (looks random)\n",
               meter_id, partial);
    }
    close(lfd);

    /* Check all meters reported in */
    for (int i = 0; i < n; i++) {
        if (!received_from[i]) {
            fprintf(stderr, "[operator] never received from meter %d!\n", i);
            return 1;
        }
    }

    /* Reconstruct: aggregate = sum of all partial sums mod p */
    field_t aggregate = 0;
    for (int i = 0; i < n; i++) {
        aggregate = field_add(aggregate, partials[i]);
    }

    double t_end = now_ms();

    printf("\n[operator] ============================\n");
    printf("[operator] AGGREGATE CONSUMPTION = %u\n", aggregate);
    printf("[operator] Meters                = %d\n", n);
    printf("[operator] Total time            = %.3f ms\n", t_end - t_start);
    printf("[operator] ============================\n");
    printf("[operator] (No meter sent its raw value over the network)\n\n");

    return 0;
}
