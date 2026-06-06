#ifndef MPC_LIMITS_H
#define MPC_LIMITS_H

/*
 * mpc_limits.h  --  Shared compile-time limits for the MPC testbed.
 *
 * Keep these in ONE place so the comm layer, the runner, and the apps all
 * agree. A mismatch (e.g. the comm layer sizing an array at 32 while the
 * runner allows ids up to 255) silently corrupts memory on high-N runs.
 */

/*
 * Maximum number of parties in an all-party MPC run
 * (bench_mpc / meter_node / mpc_runner).
 *
 * This value bounds BOTH:
 *   - _peer_fd[] in the TCP comm layer (mpc_comm_tcp.c), and
 *   - the per-party share / received / ip / port arrays in the runner.
 *
 * They MUST use the same value. Party i listens on MPC_BASE_PORT + i, so this
 * also implies ports MPC_BASE_PORT .. MPC_BASE_PORT + MPC_MAX_PARTIES - 1.
 */
#define MPC_MAX_PARTIES 256

/*
 * Maximum number of simulated clients for the central secure aggregation path
 * (secure_agg_server). These connect to a single aggregator port, so this is
 * independent of MPC_MAX_PARTIES.
 */
#define MPC_MAX_CLIENTS 5000

/* Largest cluster config file (in bytes) accepted by the JSON loader. */
#define MPC_CONFIG_BUF_SIZE 65536

#endif /* MPC_LIMITS_H */
