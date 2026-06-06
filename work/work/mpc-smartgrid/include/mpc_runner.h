#ifndef MPC_RUNNER_H
#define MPC_RUNNER_H

#include "mpc_field.h"

typedef struct {
    int n_parties;
    field_t local_partial;
    field_t global_aggregate;
    double protocol_ms;
} mpc_run_result_t;

int mpc_run_from_config(int my_id,
                        const char *cfg_path,
                        field_t my_value,
                        mpc_run_result_t *out);

#endif
