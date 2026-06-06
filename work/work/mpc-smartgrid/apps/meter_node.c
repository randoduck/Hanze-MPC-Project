#include "mpc_runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/resource.h>

/* strtol with validation: reject non-numeric / negative / overflow ids. */
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

static int read_first_meter_value(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        perror(path);
        exit(1);
    }

    char line[256];

    if (!fgets(line, sizeof(line), fp)) {
        fprintf(stderr, "[meter] empty CSV: %s\n", path);
        fclose(fp);
        exit(1);
    }

    if (!fgets(line, sizeof(line), fp)) {
        fprintf(stderr, "[meter] no reading row in CSV: %s\n", path);
        fclose(fp);
        exit(1);
    }

    fclose(fp);

    char timestamp[128];
    int meter_id = -1;
    int energy_wh = -1;

    if (sscanf(line, "%127[^,],%d,%d", timestamp, &meter_id, &energy_wh) != 3) {
        fprintf(stderr, "[meter] malformed CSV row: %s\n", line);
        exit(1);
    }

    if (energy_wh < 0) {
        fprintf(stderr, "[meter] invalid negative reading: %d\n", energy_wh);
        exit(1);
    }

    return energy_wh;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s --id <i> --config <path> --input <csv>\n"
        "  --id      party id from cluster config\n"
        "  --config  cluster JSON file\n"
        "  --input   smart-meter CSV input file\n",
        prog);
}

int main(int argc, char **argv) {
    int my_id = -1;
    const char *cfg_path = NULL;
    const char *input_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            if (parse_int_arg(argv[++i], &my_id) != 0) {
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            cfg_path = argv[++i];
        } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input_path = argv[++i];
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (my_id < 0 || !cfg_path || !input_path) {
        print_usage(argv[0]);
        return 1;
    }

    int meter_value = read_first_meter_value(input_path);

    if ((unsigned long)meter_value >= MPC_PRIME) {
        fprintf(stderr, "[meter] reading must be in [0, p-1]\n");
        return 1;
    }

    struct rusage ru_before;
    struct rusage ru_after;
    getrusage(RUSAGE_SELF, &ru_before);

    mpc_run_result_t result;
    int rc = mpc_run_from_config(my_id, cfg_path, (field_t)meter_value, &result);

    getrusage(RUSAGE_SELF, &ru_after);

    if (rc != 0) {
        fprintf(stderr, "[meter] MPC failed with rc=%d\n", rc);
        return 1;
    }

    printf("[meter] ============================\n");
    printf("[meter] node_id          = %d\n", my_id);
    printf("[meter] input_energy_wh  = %u\n", (field_t)meter_value);
    printf("[meter] Local Partial Sum= %u\n", result.local_partial);
    printf("[meter] GLOBAL AGGREGATE = %u\n", result.global_aggregate);
    printf("[meter] protocol_ms      = %.3f\n", result.protocol_ms);
    printf("[meter] n_parties        = %d\n", result.n_parties);
    printf("[meter] max_rss_kb       = %ld\n", ru_after.ru_maxrss);
    printf("[meter] user_cpu_sec     = %ld.%06ld\n",
           (long)ru_after.ru_utime.tv_sec,
           (long)ru_after.ru_utime.tv_usec);
    printf("[meter] sys_cpu_sec      = %ld.%06ld\n",
           (long)ru_after.ru_stime.tv_sec,
           (long)ru_after.ru_stime.tv_usec);
    printf("[meter] ============================\n\n");

    return 0;
}
