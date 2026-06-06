/*
 * test_uc1.c  --  UC-1: Private Consumption Aggregation (2-party, TCP)
 *
 * Run on BOTH Raspberry Pis simultaneously:
 *
 *   Pi 0:  ./build/test_uc1 0 192.168.1.10 192.168.1.11 300
 *   Pi 1:  ./build/test_uc1 1 192.168.1.10 192.168.1.11 700
 *
 * Both parties print the same aggregate (1000). Neither sees the other's value.
 *
 * Protocol (additive sharing, 2 parties, 1 network round):
 *   1. Party i splits v_i into shares s_i[0], s_i[1]  (sum = v_i mod p)
 *   2. Lower-id party sends first (prevents deadlock):
 *        Party 0: send s0[1] -> party 1,  recv s1[0] <- party 1
 *        Party 1: recv s0[1] <- party 0,  send s1[0] -> party 0
 *   3. Each party computes partial_i = s_i[i] + received_share  mod p
 *   4. Exchange partial sums (same send-order convention)
 *   5. aggregate = partial_0 + partial_1 = v_0 + v_1  mod p
 */

#include "mpc_field.h"
#include "mpc_share.h"
#include "mpc_comm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr,
            "Usage: %s <my_id> <party0_ip> <party1_ip> <my_value>\n"
            "  my_id      : 0 or 1\n"
            "  party0_ip  : IPv4 address of party 0\n"
            "  party1_ip  : IPv4 address of party 1\n"
            "  my_value   : this party's private value (integer)\n"
            "\nExample:\n"
            "  Pi 0:  %s 0 192.168.1.10 192.168.1.11 300\n"
            "  Pi 1:  %s 1 192.168.1.10 192.168.1.11 700\n",
            argv[0], argv[0], argv[0]);
        return 1;
    }

    /* strtol with validation instead of atoi/atol so a non-numeric argument
     * is rejected rather than silently parsed as 0. */
    char *e_id = NULL, *e_val = NULL;
    long  my_id_l = strtol(argv[1], &e_id, 10);
    long  val_raw = strtol(argv[4], &e_val, 10);

    if (argv[1][0] == '\0' || *e_id != '\0' ||
        argv[4][0] == '\0' || *e_val != '\0') {
        fprintf(stderr, "error: my_id and value must be integers\n");
        return 1;
    }

    int my_id = (int)my_id_l;

    if (my_id < 0 || my_id > 1) {
        fprintf(stderr, "error: my_id must be 0 or 1\n"); return 1;
    }
    if (val_raw < 0 || (unsigned long)val_raw >= MPC_PRIME) {
        fprintf(stderr, "error: value must be in [0, p-1]\n"); return 1;
    }

    field_t my_value = (field_t)val_raw;
    int     other    = 1 - my_id;

    const char *party_ips[2];
    party_ips[0] = argv[2];
    party_ips[1] = argv[3];

    printf("[UC-1] Party %d  value=%u\n", my_id, my_value);
    printf("[UC-1] Connecting...\n");

    if (comm_init(my_id, 2, party_ips) != 0) {
        fprintf(stderr, "comm_init failed\n"); return 1;
    }
    printf("[UC-1] Connected. Running protocol.\n\n");

    double t_start = now_ms();

    /* Step 1: split */
    field_t my_shares[2];
    share_split(my_value, 2, my_shares);

    /* Step 2: exchange shares (lower id sends first) */
    field_t received_share;
    if (my_id == 0) {
        {
            uint32_t wire = field_hton(my_shares[other]);
            if (comm_send(other, &wire, sizeof(wire)) != 0) {
                fprintf(stderr, "[UC-1] send failed\n"); comm_close(); return 1;
            }
        }
        {
            uint32_t wire;
            if (comm_recv(other, &wire, sizeof(wire), MPC_RECV_TIMEOUT) != 0) {
                fprintf(stderr, "[UC-1] recv failed\n"); comm_close(); return 1;
            }
            received_share = field_ntoh(wire);
        }
    } else {
        {
            uint32_t wire;
            if (comm_recv(other, &wire, sizeof(wire), MPC_RECV_TIMEOUT) != 0) {
                fprintf(stderr, "[UC-1] recv failed\n"); comm_close(); return 1;
            }
            received_share = field_ntoh(wire);
        }
        {
            uint32_t wire = field_hton(my_shares[other]);
            if (comm_send(other, &wire, sizeof(wire)) != 0) {
                fprintf(stderr, "[UC-1] send failed\n"); comm_close(); return 1;
            }
        }
    }

    /* Step 3: partial sum */
    field_t partial = field_add(my_shares[my_id], received_share);
    printf("[UC-1] Partial sum (not the secret): %u\n", partial);

    /* Step 4: exchange partial sums */
    field_t other_partial;
    if (my_id == 0) {
        {
            uint32_t wire = field_hton(partial);
            if (comm_send(other, &wire, sizeof(wire)) != 0) {
                fprintf(stderr, "[UC-1] send partial failed\n"); comm_close(); return 1;
            }
        }
        {
            uint32_t wire;
            if (comm_recv(other, &wire, sizeof(wire), MPC_RECV_TIMEOUT) != 0) {
                fprintf(stderr, "[UC-1] recv partial failed\n"); comm_close(); return 1;
            }
            other_partial = field_ntoh(wire);
        }
    } else {
        {
            uint32_t wire;
            if (comm_recv(other, &wire, sizeof(wire), MPC_RECV_TIMEOUT) != 0) {
                fprintf(stderr, "[UC-1] recv partial failed\n"); comm_close(); return 1;
            }
            other_partial = field_ntoh(wire);
        }
        {
            uint32_t wire = field_hton(partial);
            if (comm_send(other, &wire, sizeof(wire)) != 0) {
                fprintf(stderr, "[UC-1] send partial failed\n"); comm_close(); return 1;
            }
        }
    }

    /* Step 5: reconstruct */
    field_t aggregate = field_add(partial, other_partial);
    double  t_end     = now_ms();

    printf("\n[UC-1] ============================\n");
    printf("[UC-1] AGGREGATE    = %u\n", aggregate);
    printf("[UC-1] Protocol ms  = %.3f\n", t_end - t_start);
    printf("[UC-1] ============================\n\n");

    comm_close();
    return 0;
}
