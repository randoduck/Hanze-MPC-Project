#ifndef MPC_STATUS_H
#define MPC_STATUS_H

/*
 * mpc_status.h  --  Structured return / error codes.
 *
 * Returning a specific code (instead of a bare 1) lets the benchmark scripts
 * classify a failure from the process exit code alone, without parsing logs.
 */

typedef enum {
    MPC_OK            = 0,
    MPC_ERR_CONFIG    = 1,   /* bad / missing / malformed cluster config   */
    MPC_ERR_CONNECT   = 2,   /* could not establish peer connections       */
    MPC_ERR_SEND      = 3,   /* send failed mid-protocol                   */
    MPC_ERR_RECV      = 4,   /* recv failed mid-protocol                   */
    MPC_ERR_TIMEOUT   = 5,   /* recv timed out waiting for a peer          */
    MPC_ERR_AGGREGATE = 6,   /* reconstructed aggregate did not match      */
    MPC_ERR_ARGS      = 7    /* bad command-line arguments                 */
} mpc_status_t;

static inline const char *mpc_status_str(int s) {
    switch (s) {
        case MPC_OK:            return "OK";
        case MPC_ERR_CONFIG:    return "ERR_CONFIG";
        case MPC_ERR_CONNECT:   return "ERR_CONNECT";
        case MPC_ERR_SEND:      return "ERR_SEND";
        case MPC_ERR_RECV:      return "ERR_RECV";
        case MPC_ERR_TIMEOUT:   return "ERR_TIMEOUT";
        case MPC_ERR_AGGREGATE: return "ERR_AGGREGATE";
        case MPC_ERR_ARGS:      return "ERR_ARGS";
        default:                return "ERR_UNKNOWN";
    }
}

#endif /* MPC_STATUS_H */
