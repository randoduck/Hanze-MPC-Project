#include "mpc_field.h"
#include "mpc_limits.h"
#include "secure_agg.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s --n N --port PORT --expected EXPECTED\n"
        "          [--timeout-ms MS] [--test-id ID] [--missing-out PATH]\n"
        "  --timeout-ms  stop waiting after MS milliseconds (default: wait forever)\n"
        "  --test-id     reject submissions whose test_id does not match\n"
        "  --missing-out write comma-separated missing ids to PATH on completion\n",
        prog);
}

/* strtol-based parsers so a bad argument fails loudly instead of via atoi's
 * silent 0. */
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

static int parse_u32_arg(const char *s, uint32_t *out) {
    errno = 0;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 0);
    if (end == s || *end != '\0' || errno != 0 || v > 0xFFFFFFFFUL) {
        return -1;
    }
    *out = (uint32_t)v;
    return 0;
}

static int recv_full(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;

    while (got < len) {
        ssize_t r = recv(fd, p + got, len - got, 0);
        if (r == 0) return -1;
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        got += (size_t)r;
    }

    return 0;
}

/* Write the missing ids (those never seen) as a comma-separated list. */
static void write_missing_file(const char *path, const uint8_t *seen, int n) {
    FILE *fp = fopen(path, "w");
    if (!fp) {
        perror(path);
        return;
    }
    int first = 1;
    for (int i = 0; i < n; i++) {
        if (!seen[i]) {
            fprintf(fp, "%s%d", first ? "" : ",", i);
            first = 0;
        }
    }
    fprintf(fp, "\n");
    fclose(fp);
}

int main(int argc, char **argv) {
    int n = -1;
    int port = -1;
    unsigned long expected_raw = 0;
    int have_expected = 0;
    int timeout_ms = -1;          /* -1 => wait forever (backward compatible) */
    uint32_t test_id = 0;
    int check_test_id = 0;
    const char *missing_out = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--n") == 0 && i + 1 < argc) {
            if (parse_int_arg(argv[++i], &n) != 0) { usage(argv[0]); return 1; }
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            if (parse_int_arg(argv[++i], &port) != 0) { usage(argv[0]); return 1; }
        } else if (strcmp(argv[i], "--expected") == 0 && i + 1 < argc) {
            errno = 0;
            char *end = NULL;
            expected_raw = strtoul(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || errno != 0) { usage(argv[0]); return 1; }
            have_expected = 1;
        } else if (strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc) {
            if (parse_int_arg(argv[++i], &timeout_ms) != 0) { usage(argv[0]); return 1; }
        } else if (strcmp(argv[i], "--test-id") == 0 && i + 1 < argc) {
            if (parse_u32_arg(argv[++i], &test_id) != 0) { usage(argv[0]); return 1; }
            check_test_id = 1;
        } else if (strcmp(argv[i], "--missing-out") == 0 && i + 1 < argc) {
            missing_out = argv[++i];
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (n <= 0 || n > MPC_MAX_CLIENTS || port <= 0 || !have_expected) {
        usage(argv[0]);
        return 1;
    }

    uint8_t *seen = calloc((size_t)n, sizeof(uint8_t));
    if (!seen) {
        perror("calloc seen");
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        free(seen);
        return 1;
    }

    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        close(server_fd);
        free(seen);
        return 1;
    }

    if (listen(server_fd, 4096) != 0) {
        perror("listen");
        close(server_fd);
        free(seen);
        return 1;
    }

    printf("[agg] secure aggregation server\n");
    printf("[agg] N=%d port=%d expected=%lu timeout_ms=%d test_id=%u\n",
           n, port, expected_raw, timeout_ms, check_test_id ? test_id : 0u);
    printf("[agg] waiting for submissions...\n");
    fflush(stdout);

    double t0 = now_ms();

    uint64_t aggregate = 0;
    int received = 0;
    int duplicates = 0;
    int invalid = 0;
    int timed_out = 0;

    while (received < n) {
        /* Wait for the next connection, honoring the overall deadline. A
         * dropped client can no longer hang the server forever. */
        int wait_ms = -1;
        if (timeout_ms >= 0) {
            double left = (double)timeout_ms - (now_ms() - t0);
            if (left <= 0) { timed_out = 1; break; }
            wait_ms = (int)left;
        }

        struct pollfd pfd;
        pfd.fd = server_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int pr = poll(&pfd, 1, wait_ms);
        if (pr == 0) { timed_out = 1; break; }
        if (pr < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }

        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);

        int client_fd = accept(server_fd, (struct sockaddr *)&peer, &peer_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            invalid++;
            continue;
        }

        uint8_t wire[SECURE_AGG_WIRE_SIZE];
        if (recv_full(client_fd, wire, sizeof(wire)) != 0) {
            close(client_fd);
            invalid++;
            continue;
        }

        close(client_fd);

        secure_agg_packet_t pkt;
        secure_agg_unpack(wire, &pkt);

        if (pkt.magic != SECURE_AGG_MAGIC ||
            pkt.version != SECURE_AGG_VERSION ||
            pkt.type != SECURE_AGG_TYPE_SUBMISSION ||
            pkt.id >= (uint32_t)n ||
            pkt.masked_value >= MPC_PRIME ||
            (check_test_id && pkt.test_id != test_id)) {
            invalid++;
            continue;
        }

        if (seen[pkt.id]) {
            duplicates++;
            continue;
        }

        seen[pkt.id] = 1;
        received++;

        aggregate += pkt.masked_value;
        aggregate %= MPC_PRIME;

        if (received % 100 == 0 || received == n) {
            printf("[agg] received=%d/%d\n", received, n);
            fflush(stdout);
        }
    }

    double t1 = now_ms();

    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);

    int missing = n - received;

    uint64_t expected = expected_raw % MPC_PRIME;
    const char *status =
        (received == n && aggregate == expected) ? "PASS" : "FAIL";

    printf("[agg] ============================\n");
    printf("[agg] RECEIVED_COUNT      = %d\n", received);
    printf("[agg] MISSING_COUNT       = %d\n", missing);
    printf("[agg] DUPLICATES          = %d\n", duplicates);
    printf("[agg] INVALID             = %d\n", invalid);
    printf("[agg] TIMED_OUT           = %d\n", timed_out);
    printf("[agg] EXPECTED_AGGREGATE  = %lu\n", (unsigned long)expected);
    printf("[agg] OBSERVED_AGGREGATE  = %lu\n", (unsigned long)aggregate);
    printf("[agg] WALL_MS             = %.3f\n", t1 - t0);
    printf("[agg] MAX_RSS_KB          = %ld\n", ru.ru_maxrss);
    printf("[agg] STATUS              = %s\n", status);

    if (missing > 0) {
        /* Print up to the first 50 missing ids inline; the full list goes to
         * the file so large failures stay debuggable. */
        printf("[agg] MISSING_IDS         = ");
        int printed = 0;
        for (int i = 0; i < n && printed < 50; i++) {
            if (!seen[i]) {
                printf("%s%d", printed ? "," : "", i);
                printed++;
            }
        }
        if (missing > printed) printf(",...(%d more)", missing - printed);
        printf("\n");

        if (missing_out) {
            write_missing_file(missing_out, seen, n);
            printf("[agg] MISSING_IDS_FILE    = %s\n", missing_out);
        }
    }

    printf("[agg] ============================\n");

    close(server_fd);
    free(seen);

    return strcmp(status, "PASS") == 0 ? 0 : 1;
}
