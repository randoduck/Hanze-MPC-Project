/*
 * test_uc1.c  --  UC-1: Smart Meter (party 0 or party 1)
 *
 * The meter splits its private value, sends one share to the other meter,
 * computes its partial sum, then sends that partial sum to the grid operator.
 * The meter NEVER sees the aggregate -- only the operator does.
 *
 * Usage:
 *   ./build/test_uc1 <my_id> <meter0_ip> <meter1_ip> <operator_ip> <my_value>
 *
 * Example (meter 0 = 192.168.1.10, meter 1 = 192.168.1.11, operator = 192.168.1.12):
 *   Meter 0:    ./build/test_uc1 0 192.168.1.10 192.168.1.11 192.168.1.12 300
 *   Meter 1:    ./build/test_uc1 1 192.168.1.10 192.168.1.11 192.168.1.12 700
 *   Operator:   ./build/test_operator 192.168.1.10 192.168.1.11
 *
 * Localhost simulation (3 terminals):
 *   Terminal 1: ./build/test_operator 127.0.0.1 127.0.0.1
 *   Terminal 2: ./build/test_uc1 0 127.0.0.1 127.0.0.1 127.0.0.1 300
 *   Terminal 3: ./build/test_uc1 1 127.0.0.1 127.0.0.1 127.0.0.1 700
 *
 * Protocol:
 *   1. Meters connect to each other and exchange shares (1 round).
 *   2. Each meter computes its partial sum locally.
 *   3. Each meter sends its partial sum to the operator.
 *   4. Meter prints "done" -- no aggregate printed here.
 *   5. Operator reconstructs and prints the aggregate.
 */

#include "mpc_field.h"
#include "mpc_share.h"
#include "mpc_comm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Operator listens on this port for partial sums from the meters.
 * Meter i connects to OPERATOR_PORT + i so the operator knows who sent what. */
#define OPERATOR_PORT  5010

static int connect_to_operator(const char *op_ip, int my_id) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)(OPERATOR_PORT + my_id));
    inet_pton(AF_INET, op_ip, &addr.sin_addr);

    int attempts = 0;
    while (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (++attempts % 10 == 0)
            printf("[meter %d] waiting for operator...\n", my_id);
        struct timespec _ts = {0, 500000000L}; nanosleep(&_ts, NULL);
    }
    return fd;
}

int main(int argc, char *argv[]) {
    if (argc != 6) {
        fprintf(stderr,
            "Usage: %s <my_id> <meter0_ip> <meter1_ip> <operator_ip> <my_value>\n"
            "\nExample (localhost):\n"
            "  Terminal 1: ./build/test_operator 127.0.0.1 127.0.0.1\n"
            "  Terminal 2: ./build/test_uc1 0 127.0.0.1 127.0.0.1 127.0.0.1 300\n"
            "  Terminal 3: ./build/test_uc1 1 127.0.0.1 127.0.0.1 127.0.0.1 700\n",
            argv[0]);
        return 1;
    }

    int   my_id      = atoi(argv[1]);
    const char *op_ip = argv[4];
    long  val_raw    = atol(argv[5]);

    if (my_id < 0 || my_id > 1) {
        fprintf(stderr, "error: my_id must be 0 or 1\n"); return 1;
    }
    if (val_raw < 0 || (unsigned long)val_raw >= MPC_PRIME) {
        fprintf(stderr, "error: value must be in [0, p-1]\n"); return 1;
    }

    field_t my_value = (field_t)val_raw;
    int     other    = 1 - my_id;

    const char *meter_ips[2];
    meter_ips[0] = argv[2];
    meter_ips[1] = argv[3];

    printf("[meter %d] value=%u  (private, never leaves this device)\n",
           my_id, my_value);

    /* ---- Step 1: connect to other meter ---- */
    printf("[meter %d] connecting to other meter...\n", my_id);
    if (comm_init(my_id, 2, meter_ips) != 0) {
        fprintf(stderr, "comm_init failed\n"); return 1;
    }
    printf("[meter %d] meter-to-meter link up.\n", my_id);

    /* ---- Step 2: split and exchange shares with other meter ---- */
    field_t my_shares[2];
    share_split(my_value, 2, my_shares);

    field_t received_share;
    if (my_id == 0) {
        comm_send(other, &my_shares[other], sizeof(field_t));
        comm_recv(other, &received_share, sizeof(field_t), MPC_RECV_TIMEOUT);
    } else {
        comm_recv(other, &received_share, sizeof(field_t), MPC_RECV_TIMEOUT);
        comm_send(other, &my_shares[other], sizeof(field_t));
    }
    comm_close();

    /* ---- Step 3: compute partial sum locally ---- */
    field_t partial = field_add(my_shares[my_id], received_share);
    printf("[meter %d] partial sum computed (random-looking: %u)\n",
           my_id, partial);

    /* ---- Step 4: send partial sum to operator ---- */
    printf("[meter %d] connecting to operator at %s...\n", my_id, op_ip);
    int op_fd = connect_to_operator(op_ip, my_id);
    send(op_fd, &partial, sizeof(field_t), 0);
    close(op_fd);

    printf("[meter %d] partial sum sent. Protocol complete.\n", my_id);
    printf("[meter %d] ** This device does NOT know the aggregate. **\n", my_id);

    return 0;
}
