#include "mpc_runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

/* strtol with validation: reject non-numeric / negative / overflow. */
static int parse_int_arg(const char *s, int *out) {
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno != 0 || v < 0 || v > INT_MAX) {
        return -1;
    }
    *out = (int)v;
    return 0;
}

static int parse_long_arg(const char *s, long *out) {
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno != 0 || v < 0) {
        return -1;
    }
    *out = v;
    return 0;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s --id <i> --config <path> --secret <v>\n"
        "  --id      this party's 0-indexed id\n"
        "  --config  cluster JSON file\n"
        "  --secret  this party's private value, integer in [0, p-1]\n",
        prog);
}

int main(int argc, char *argv[]) {
    int my_id = -1;
    long val_raw = -1;
    const char *cfg_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            if (parse_int_arg(argv[++i], &my_id) != 0) {
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            cfg_path = argv[++i];
        } else if (strcmp(argv[i], "--secret") == 0 && i + 1 < argc) {
            if (parse_long_arg(argv[++i], &val_raw) != 0) {
                print_usage(argv[0]);
                return 1;
            }
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (my_id < 0 || !cfg_path || val_raw < 0) {
        print_usage(argv[0]);
        return 1;
    }

    if ((unsigned long)val_raw >= MPC_PRIME) {
        fprintf(stderr, "error: --secret must be in [0, p-1]\n");
        return 1;
    }

    mpc_run_result_t result;
    int rc = mpc_run_from_config(my_id, cfg_path, (field_t)val_raw, &result);

    if (rc != 0) {
        fprintf(stderr, "[bench] MPC failed with rc=%d\n", rc);
        return 1;
    }

    printf("[bench] ============================\n");
    printf("[bench] Local Partial Sum   = %u\n", result.local_partial);
    printf("[bench] GLOBAL AGGREGATE    = %u\n", result.global_aggregate);
    printf("[bench] protocol_ms         = %.3f\n", result.protocol_ms);
    printf("[bench] ============================\n\n");

    return 0;
}
