#include "mpc_runner.h"

#include "mpc_share.h"
#include "mpc_comm.h"
#include "mpc_limits.h"
#include "mpc_status.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/resource.h>

/* Sized from the single shared limit so this can never drift from the comm
 * layer's _peer_fd[] array. */
#define RUNNER_MAX_PARTIES MPC_MAX_PARTIES

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static int parse_int_after_colon(const char *kw, const char *end) {
    if (!kw) return -1;
    const char *colon = strchr(kw, ':');
    if (!colon || colon >= end) return -1;

    /* strtol with validation instead of atoi: reject non-numeric, overflow,
     * and negative values so a malformed config fails loudly, not silently. */
    errno = 0;
    char *stop = NULL;
    long v = strtol(colon + 1, &stop, 10);
    if (stop == colon + 1 || errno != 0 || v < 0 || v > INT_MAX) {
        return -1;
    }
    return (int)v;
}

static int parse_string_after_colon(const char *kw, const char *end,
                                    char *out, size_t out_sz) {
    if (!kw) return -1;
    const char *colon = strchr(kw, ':');
    if (!colon || colon >= end) return -1;
    const char *q1 = strchr(colon, '"');
    if (!q1 || q1 >= end) return -1;
    q1++;
    const char *q2 = strchr(q1, '"');
    if (!q2 || q2 >= end) return -1;

    size_t len = (size_t)(q2 - q1);
    if (len >= out_sz) len = out_sz - 1;

    memcpy(out, q1, len);
    out[len] = 0;
    return 0;
}

static int load_cluster_config(const char *path,
                               char *ips[RUNNER_MAX_PARTIES],
                               int ports[RUNNER_MAX_PARTIES]) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        perror(path);
        return -1;
    }

    char buf[MPC_CONFIG_BUF_SIZE];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    int more = (fgetc(fp) != EOF);   /* anything left => file too big */
    fclose(fp);

    if (n == 0) {
        fprintf(stderr, "%s: empty file\n", path);
        return -1;
    }

    if (more) {
        fprintf(stderr, "%s: config larger than %d-byte buffer\n",
                path, MPC_CONFIG_BUF_SIZE);
        return -1;
    }

    buf[n] = 0;

    const char *p = strchr(buf, '[');
    if (!p) {
        fprintf(stderr, "%s: missing '[' for nodes array\n", path);
        return -1;
    }
    p++;

    int max_id = -1;

    while (1) {
        const char *obj = strchr(p, '{');
        if (!obj) break;

        const char *end = strchr(obj, '}');
        if (!end) break;

        const char *id_kw   = strstr(obj, "\"id\"");
        const char *host_kw = strstr(obj, "\"host\"");
        const char *port_kw = strstr(obj, "\"port\"");

        if (id_kw && id_kw >= end) id_kw = NULL;
        if (host_kw && host_kw >= end) host_kw = NULL;
        if (port_kw && port_kw >= end) port_kw = NULL;

        int id = parse_int_after_colon(id_kw, end);
        int port = parse_int_after_colon(port_kw, end);

        char host[64] = {0};
        int got_host = host_kw &&
            parse_string_after_colon(host_kw, end, host, sizeof(host)) == 0;

        if (id < 0 || !got_host || port <= 0) {
            fprintf(stderr, "%s: malformed node entry near offset %ld\n",
                    path, (long)(obj - buf));
            return -1;
        }

        if (id >= RUNNER_MAX_PARTIES) {
            fprintf(stderr, "%s: id %d exceeds compiled max %d\n",
                    path, id, RUNNER_MAX_PARTIES - 1);
            return -1;
        }

        if (ips[id]) {
            fprintf(stderr, "%s: duplicate id %d\n", path, id);
            return -1;
        }

        ips[id] = strdup(host);
        ports[id] = port;

        if (id > max_id) max_id = id;

        p = end + 1;
    }

    if (max_id < 0) {
        fprintf(stderr, "%s: no node entries parsed\n", path);
        return -1;
    }

    int N = max_id + 1;

    for (int i = 0; i < N; i++) {
        if (!ips[i]) {
            fprintf(stderr, "%s: gap in ids, missing id %d\n", path, i);
            return -1;
        }
    }

    return N;
}

static int append_perf_log(int my_id,
                           int n_parties,
                           field_t input_value,
                           double protocol_ms,
                           field_t local_partial,
                           field_t global_aggregate,
                           long max_rss_kb,
                           int status_code) {
    char path[64];
    snprintf(path, sizeof(path), "logs/node_%d.csv", my_id);

    struct stat st;
    int is_new = (stat(path, &st) != 0 || st.st_size == 0);

    FILE *fp = fopen(path, "a");
    if (!fp) {
        perror(path);
        return -1;
    }

    if (is_new) {
        fprintf(fp,
            "timestamp,test_id,id,n_parties,input,protocol_ms,"
            "local_partial,global_aggregate,max_rss_kb,status,error_code\n");
    }

    time_t t = time(NULL);
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);

    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm_buf);

    /* test_id comes from the harness via the environment so scripts can
     * correlate a log line with a specific run without changing the API. */
    const char *test_id = getenv("MPC_TEST_ID");
    if (!test_id) test_id = "0";

    fprintf(fp, "%s,%s,%d,%d,%u,%.3f,%u,%u,%ld,%s,%d\n",
            ts, test_id, my_id, n_parties, input_value, protocol_ms,
            local_partial, global_aggregate, max_rss_kb,
            mpc_status_str(status_code), status_code);

    fclose(fp);
    return 0;
}

static void free_party_ips(char *party_ips[RUNNER_MAX_PARTIES], int n_parties) {
    for (int i = 0; i < n_parties; i++) {
        free(party_ips[i]);
        party_ips[i] = NULL;
    }
}

int mpc_run_from_config(int my_id,
                        const char *cfg_path,
                        field_t my_value,
                        mpc_run_result_t *out) {
    if (!cfg_path || !out) {
        return MPC_ERR_ARGS;
    }

    char *party_ips[RUNNER_MAX_PARTIES] = {0};
    int party_ports[RUNNER_MAX_PARTIES] = {0};

    int N = load_cluster_config(cfg_path, party_ips, party_ports);
    if (N < 0) {
        return MPC_ERR_CONFIG;
    }

    if (my_id < 0 || my_id >= N) {
        fprintf(stderr, "error: --id %d out of range, cluster has %d parties\n", my_id, N);
        free_party_ips(party_ips, N);
        return MPC_ERR_ARGS;
    }

    for (int i = 0; i < N; i++) {
        if (party_ports[i] != MPC_BASE_PORT + i) {
            fprintf(stderr,
                "[mpc_runner] warning: party %d port %d != MPC_BASE_PORT+%d (%d); "
                "comm layer will use %d\n",
                i, party_ports[i], i, MPC_BASE_PORT + i, MPC_BASE_PORT + i);
        }
    }

    printf("[mpc_runner] party %d/%d value=%u config=%s\n",
           my_id, N, my_value, cfg_path);
    printf("[mpc_runner] Connecting...\n");

    if (comm_init(my_id, N, (const char **)party_ips) != 0) {
        fprintf(stderr, "comm_init failed\n");
        free_party_ips(party_ips, N);
        return MPC_ERR_CONNECT;
    }

    printf("[mpc_runner] Connected. Running protocol.\n\n");

    double t_start = now_ms();

    field_t my_shares[RUNNER_MAX_PARTIES];
    share_split(my_value, N, my_shares);

    field_t received[RUNNER_MAX_PARTIES] = {0};

    for (int j = 0; j < N; j++) {
        if (j == my_id) continue;

        if (my_id < j) {
            uint32_t wire_out = field_hton(my_shares[j]);
            if (comm_send(j, &wire_out, sizeof(wire_out)) != 0) {
                fprintf(stderr, "[mpc_runner] send to %d failed\n", j);
                comm_close();
                free_party_ips(party_ips, N);
                return MPC_ERR_SEND;
            }

            uint32_t wire_in;
            if (comm_recv(j, &wire_in, sizeof(wire_in), MPC_RECV_TIMEOUT) != 0) {
                fprintf(stderr, "[mpc_runner] recv from %d failed\n", j);
                comm_close();
                free_party_ips(party_ips, N);
                return MPC_ERR_RECV;
            }

            received[j] = field_ntoh(wire_in);
        } else {
            uint32_t wire_in;
            if (comm_recv(j, &wire_in, sizeof(wire_in), MPC_RECV_TIMEOUT) != 0) {
                fprintf(stderr, "[mpc_runner] recv from %d failed\n", j);
                comm_close();
                free_party_ips(party_ips, N);
                return MPC_ERR_RECV;
            }

            received[j] = field_ntoh(wire_in);

            uint32_t wire_out = field_hton(my_shares[j]);
            if (comm_send(j, &wire_out, sizeof(wire_out)) != 0) {
                fprintf(stderr, "[mpc_runner] send to %d failed\n", j);
                comm_close();
                free_party_ips(party_ips, N);
                return MPC_ERR_SEND;
            }
        }
    }

    field_t partial = my_shares[my_id];

    for (int j = 0; j < N; j++) {
        if (j != my_id) {
            partial = field_add(partial, received[j]);
        }
    }

    field_t final_aggregate = partial;

    for (int j = 0; j < N; j++) {
        if (j == my_id) continue;

        if (my_id < j) {
            uint32_t wire_out = field_hton(partial);
            if (comm_send(j, &wire_out, sizeof(wire_out)) != 0) {
                fprintf(stderr, "[mpc_runner] partial-sum send to %d failed\n", j);
                comm_close();
                free_party_ips(party_ips, N);
                return MPC_ERR_SEND;
            }

            uint32_t wire_in;
            if (comm_recv(j, &wire_in, sizeof(wire_in), MPC_RECV_TIMEOUT) != 0) {
                fprintf(stderr, "[mpc_runner] partial-sum recv from %d failed\n", j);
                comm_close();
                free_party_ips(party_ips, N);
                return MPC_ERR_RECV;
            }

            final_aggregate = field_add(final_aggregate, field_ntoh(wire_in));
        } else {
            uint32_t wire_in;
            if (comm_recv(j, &wire_in, sizeof(wire_in), MPC_RECV_TIMEOUT) != 0) {
                fprintf(stderr, "[mpc_runner] partial-sum recv from %d failed\n", j);
                comm_close();
                free_party_ips(party_ips, N);
                return MPC_ERR_RECV;
            }

            final_aggregate = field_add(final_aggregate, field_ntoh(wire_in));

            uint32_t wire_out = field_hton(partial);
            if (comm_send(j, &wire_out, sizeof(wire_out)) != 0) {
                fprintf(stderr, "[mpc_runner] partial-sum send to %d failed\n", j);
                comm_close();
                free_party_ips(party_ips, N);
                return MPC_ERR_SEND;
            }
        }
    }

    double t_end = now_ms();
    double protocol_ms = t_end - t_start;

    out->n_parties = N;
    out->local_partial = partial;
    out->global_aggregate = final_aggregate;
    out->protocol_ms = protocol_ms;

    struct rusage ru;
    long max_rss_kb = (getrusage(RUSAGE_SELF, &ru) == 0) ? ru.ru_maxrss : -1;

    if (append_perf_log(my_id, N, my_value, protocol_ms, partial,
                        final_aggregate, max_rss_kb, MPC_OK) != 0) {
        fprintf(stderr, "[mpc_runner] warning: failed to append perf log\n");
    }

    comm_close();
    free_party_ips(party_ips, N);

    return MPC_OK;
}
