/*
 * test_uc1.c  --  UC-1: Smart Meter (supports 2 to 32 meters)
 *
 * Usage:
 *   ./build/test_uc1 <my_id> <n> <meter_0_ip> ... <meter_{n-1}_ip> <operator_ip> <value>
 *
 * 2-meter localhost example:
 *   ./build/test_operator 2 127.0.0.1 127.0.0.1        (start first)
 *   ./build/test_uc1 0 2 127.0.0.1 127.0.0.1 127.0.0.1 300
 *   ./build/test_uc1 1 2 127.0.0.1 127.0.0.1 127.0.0.1 700
 *
 * 5-meter localhost example:
 *   ./build/test_operator 5 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1
 *   ./build/test_uc1 0 5 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1 100
 *   ./build/test_uc1 1 5 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1 200
 *   ./build/test_uc1 2 5 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1 300
 *   ./build/test_uc1 3 5 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1 400
 *   ./build/test_uc1 4 5 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1 127.0.0.1 500
 *   (operator prints AGGREGATE = 1500)
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

#define OPERATOR_PORT  5010
#define MAX_N          32

static int connect_to_operator(const char *op_ip) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(OPERATOR_PORT);
    inet_pton(AF_INET, op_ip, &addr.sin_addr);
    while (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        struct timespec ts = {0, 500000000L};
        nanosleep(&ts, NULL);
    }
    return fd;
}

int main(int argc, char *argv[]) {
    if (argc < 6) {
        fprintf(stderr,
            "Usage: %s <my_id> <n> <meter_0_ip> ... <meter_{n-1}_ip> <operator_ip> <value>\n",
            argv[0]);
        return 1;
    }

    int my_id = atoi(argv[1]);
    int n     = atoi(argv[2]);

    if (n < 2 || n > MAX_N) { fprintf(stderr, "n must be 2-%d\n", MAX_N); return 1; }
    if (argc != n + 5)       { fprintf(stderr, "wrong number of arguments for n=%d\n", n); return 1; }
    if (my_id < 0 || my_id >= n) { fprintf(stderr, "my_id must be 0 to n-1\n"); return 1; }

    const char *meter_ips[MAX_N];
    for (int i = 0; i < n; i++) meter_ips[i] = argv[3 + i];
    const char *op_ip   = argv[3 + n];
    long        val_raw = atol(argv[3 + n + 1]);

    if (val_raw < 0 || (unsigned long)val_raw >= MPC_PRIME) {
        fprintf(stderr, "value out of range\n"); return 1;
    }

    field_t my_value = (field_t)val_raw;
    printf("[meter %d/%d] value=%u  (private)\n", my_id, n, my_value);

    /* Connect all meters */
    if (comm_init(my_id, n, meter_ips) != 0) {
        fprintf(stderr, "comm_init failed\n"); return 1;
    }
    printf("[meter %d/%d] all meters connected\n", my_id, n);

    /* Split into n shares */
    field_t my_shares[MAX_N];
    share_split(my_value, n, my_shares);

    /* Send all shares first (4 bytes each -- fits in TCP buffer, no deadlock)
     * then receive all. */
    field_t received[MAX_N];
    received[my_id] = my_shares[my_id];

    for (int j = 0; j < n; j++) {
        if (j != my_id) comm_send(j, &my_shares[j], sizeof(field_t));
    }
    for (int j = 0; j < n; j++) {
        if (j != my_id) comm_recv(j, &received[j], sizeof(field_t), MPC_RECV_TIMEOUT);
    }
    comm_close();

    /* Compute partial sum */
    field_t partial = 0;
    for (int j = 0; j < n; j++) partial = field_add(partial, received[j]);
    printf("[meter %d/%d] partial sum: %u  (random-looking)\n", my_id, n, partial);

    /* Send [id][partial] to operator */
    int op_fd = connect_to_operator(op_ip);
    uint32_t id_net  = htonl((uint32_t)my_id);
    uint32_t p_net   = htonl(partial);
    send(op_fd, &id_net, sizeof(uint32_t), 0);
    send(op_fd, &p_net,  sizeof(uint32_t), 0);
    close(op_fd);

    printf("[meter %d/%d] done. ** Does NOT know the aggregate. **\n", my_id, n);
    return 0;
}
