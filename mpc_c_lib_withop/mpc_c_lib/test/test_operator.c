/*
 * test_operator.c  --  Grid Operator (Pi 2)
 *
 * Waits for partial sums from both smart meters, adds them together,
 * and prints the aggregate. This is the ONLY device that sees the result.
 *
 * Usage:
 *   ./build/test_operator <meter0_ip> <meter1_ip>
 *
 * Example (real Pis):
 *   ./build/test_operator 192.168.1.10 192.168.1.11
 *
 * Localhost simulation (run this FIRST, then the two meters):
 *   ./build/test_operator 127.0.0.1 127.0.0.1
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

/* Must match OPERATOR_PORT in test_uc1.c
 * Meter 0 connects to port 5010, meter 1 connects to port 5011 */
#define OPERATOR_PORT  5010

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* Open a listening socket on the given port, accept one connection,
 * receive one field_t value, close, return the value. */
static field_t accept_partial(int port, int meter_id) {
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    listen(lfd, 1);

    printf("[operator] waiting for partial sum from meter %d on port %d...\n",
           meter_id, port);

    int cfd = accept(lfd, NULL, NULL);
    close(lfd);

    field_t partial;
    recv(cfd, &partial, sizeof(field_t), MSG_WAITALL);
    close(cfd);

    printf("[operator] received partial sum from meter %d: %u  (looks random)\n",
           meter_id, partial);
    return partial;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr,
            "Usage: %s <meter0_ip> <meter1_ip>\n"
            "\nRun this BEFORE starting the meters.\n"
            "\nExample (localhost):\n"
            "  Terminal 1: ./build/test_operator 127.0.0.1 127.0.0.1\n"
            "  Terminal 2: ./build/test_uc1 0 127.0.0.1 127.0.0.1 127.0.0.1 300\n"
            "  Terminal 3: ./build/test_uc1 1 127.0.0.1 127.0.0.1 127.0.0.1 700\n",
            argv[0]);
        return 1;
    }

    printf("[operator] Grid operator ready. Waiting for meters...\n\n");

    double t_start = now_ms();

    /* Accept partial sums from meter 0 and meter 1 in parallel would need
     * threads. For simplicity we accept them sequentially -- the meters
     * retry until the operator is listening so order doesn't matter. */
    field_t partial0 = accept_partial(OPERATOR_PORT + 0, 0);
    field_t partial1 = accept_partial(OPERATOR_PORT + 1, 1);

    double t_end = now_ms();

    /* Reconstruct: aggregate = partial0 + partial1 mod p */
    field_t aggregate = field_add(partial0, partial1);

    printf("\n[operator] ============================\n");
    printf("[operator] AGGREGATE CONSUMPTION = %u\n", aggregate);
    printf("[operator] Total time = %.3f ms\n", t_end - t_start);
    printf("[operator] ============================\n");
    printf("[operator] (Neither meter sent its raw value over the network)\n\n");

    return 0;
}
