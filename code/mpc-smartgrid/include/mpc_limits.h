#ifndef MPC_LIMITS_H
#define MPC_LIMITS_H

/* Shared compile-time limits — keep here so comm layer, runner, and apps
 * always agree. A mismatch silently corrupts memory on high-N runs. */

/* Bounds _peer_fd[] in the TCP comm layer AND the runner's per-party arrays. */
#define MPC_MAX_PARTIES 256

/* Max simulated clients for the central aggregation path (one aggregator port). */
#define MPC_MAX_CLIENTS 5000

#define MPC_CONFIG_BUF_SIZE 65536

#endif /* MPC_LIMITS_H */
