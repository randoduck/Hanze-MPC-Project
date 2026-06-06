#ifndef MPC_COMM_H
#define MPC_COMM_H

/*
 * mpc_comm.h  --  Abstract communication interface for MPC
 *
 * mpyc correspondence: asyncoro MessageExchanger (send_message / receive_message)
 *
 * Implementations provided:
 *   mpc_comm_tcp.c  -- TCP sockets, Linux / Raspberry Pi  (v1)
 *
 * Planned (not in v1):
 *   mpc_comm_uart.c -- UART, embedded (MSP430, ATmega)
 *
 * Protocol convention (avoids deadlock for symmetric send/recv):
 *   The party with the LOWER id always sends first in any pairwise exchange.
 *   The test programs enforce this ordering.
 */

#include <stddef.h>
#include "mpc_limits.h"

/*
 * Maximum number of parties supported.
 * Defined once in mpc_limits.h so the comm layer and the runner cannot drift
 * apart (a mismatch corrupts _peer_fd[] on high-N runs).
 */

/*
 * Port assignment: party i listens on MPC_BASE_PORT + i.
 * Both parties must agree on this value.
 * Change here if the ports are already in use on your network.
 */
#define MPC_BASE_PORT     5000

/* Default receive timeout in milliseconds */
#define MPC_RECV_TIMEOUT  10000

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/*
 * comm_init(my_id, n, party_ips)
 *
 * Set up TCP connections to all other parties.
 * Must be called before comm_send or comm_recv.
 *
 * my_id      : this party's ID, 0-indexed
 * n          : total number of parties
 * party_ips  : array of n strings, party_ips[i] is the IPv4 address of party i
 *              (e.g. {"192.168.1.10", "192.168.1.11"})
 *
 * Connection protocol (avoids the classic n-party bind/connect deadlock):
 *   - Party i binds and listens on port MPC_BASE_PORT + i.
 *   - Party i connects (with retry) to all parties j where j < i.
 *   - Party i accepts connections from all parties j where j > i.
 *   - Connecting party sends its id (4-byte int) as a handshake so the
 *     accepting party can record which peer just connected.
 *
 * Returns 0 on success, -1 on error.
 */
int comm_init(int my_id, int n, const char **party_ips);

/*
 * comm_send(to, buf, len)
 *
 * Send exactly len bytes from buf to party `to`.
 * Blocks until all bytes are sent.
 * Returns 0 on success, -1 on error.
 */
int comm_send(int to, const void *buf, size_t len);

/*
 * comm_recv(from, buf, len, timeout_ms)
 *
 * Receive exactly len bytes from party `from` into buf.
 * Blocks until all bytes arrive or timeout_ms elapses.
 *
 * timeout_ms : pass MPC_RECV_TIMEOUT for the default.
 *              A protocol hang (dropped party) will be caught here.
 *
 * Returns 0 on success, -1 on timeout or error.
 */
int comm_recv(int from, void *buf, size_t len, int timeout_ms);

/*
 * comm_close()
 *
 * Close all peer connections and free resources.
 */
void comm_close(void);

#endif /* MPC_COMM_H */
