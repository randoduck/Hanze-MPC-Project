#include "mpc_field.h"
#include "secure_agg.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static uint64_t splitmix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static uint32_t pair_mask(uint32_t a, uint32_t b, uint64_t key) {
    uint64_t x = key;
    x ^= ((uint64_t)a + 0xA5A5A5A5ULL) << 32;
    x ^= ((uint64_t)b + 0x5A5A5A5AULL);
    x = splitmix64(x);
    return (uint32_t)(x % MPC_PRIME);
}

static uint32_t mod_add(uint32_t a, uint32_t b) {
    uint64_t s = (uint64_t)a + (uint64_t)b;
    return (uint32_t)(s % MPC_PRIME);
}

static uint32_t mod_sub(uint32_t a, uint32_t b) {
    uint64_t p = MPC_PRIME;
    return (uint32_t)((p + (uint64_t)a - (uint64_t)b) % p);
}

static uint32_t compute_masked_value(uint32_t id,
                                     uint32_t n,
                                     uint32_t value,
                                     uint64_t key) {
    uint32_t masked = value % MPC_PRIME;

    for (uint32_t j = 0; j < n; j++) {
        if (j == id) continue;

        if (id < j) {
            masked = mod_add(masked, pair_mask(id, j, key));
        } else {
            masked = mod_sub(masked, pair_mask(j, id, key));
        }
    }

    return masked;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s --id ID --n N --value VALUE --aggregator IP --port PORT "
        "--mask-key KEY [--test-id ID]\n",
        prog);
}

/* strtol-based parsers replace atoi/atol so a bad argument fails loudly. */
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

static int send_full(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;

    while (sent < len) {
        ssize_t r = send(fd, p + sent, len - sent, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)r;
    }

    return 0;
}

static void sleep_ms(int ms) {
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&req, NULL);
}

/*
 * Retry the connection with per-client jitter so 1000 clients launched at once
 * do not all reconnect in lockstep (thundering herd). The jitter is derived
 * deterministically from the client id and attempt number -- no global RNG
 * needed, and it stays reproducible.
 */
static int connect_with_retry(const char *ip, int port, int retries,
                              int base_sleep_ms, uint32_t id) {
    for (int attempt = 0; attempt < retries; attempt++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port);

        if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
            close(fd);
            return -1;
        }

        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            return fd;
        }

        close(fd);

        uint32_t jitter = (uint32_t)(splitmix64(((uint64_t)id << 20) ^
                                                (uint32_t)attempt) % 100u);
        sleep_ms(base_sleep_ms + (int)jitter);
    }

    return -1;
}

int main(int argc, char **argv) {
    int id = -1;
    int n = -1;
    long value_raw = -1;
    const char *aggregator = NULL;
    int port = -1;
    uint64_t mask_key = 0;
    int have_key = 0;
    uint32_t test_id = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            if (parse_int_arg(argv[++i], &id) != 0) { usage(argv[0]); return 1; }
        } else if (strcmp(argv[i], "--n") == 0 && i + 1 < argc) {
            if (parse_int_arg(argv[++i], &n) != 0) { usage(argv[0]); return 1; }
        } else if (strcmp(argv[i], "--value") == 0 && i + 1 < argc) {
            if (parse_long_arg(argv[++i], &value_raw) != 0) { usage(argv[0]); return 1; }
        } else if (strcmp(argv[i], "--aggregator") == 0 && i + 1 < argc) {
            aggregator = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            if (parse_int_arg(argv[++i], &port) != 0) { usage(argv[0]); return 1; }
        } else if (strcmp(argv[i], "--mask-key") == 0 && i + 1 < argc) {
            errno = 0;
            char *end = NULL;
            mask_key = strtoull(argv[++i], &end, 0);
            if (end == argv[i] || *end != '\0' || errno != 0) { usage(argv[0]); return 1; }
            have_key = 1;
        } else if (strcmp(argv[i], "--test-id") == 0 && i + 1 < argc) {
            if (parse_u32_arg(argv[++i], &test_id) != 0) { usage(argv[0]); return 1; }
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (id < 0 || n <= 0 || id >= n || value_raw < 0 ||
        !aggregator || port <= 0 || !have_key) {
        usage(argv[0]);
        return 1;
    }

    if ((unsigned long)value_raw >= MPC_PRIME) {
        fprintf(stderr, "[meter] value must be in [0, p-1]\n");
        return 1;
    }

    double t0 = now_ms();

    uint32_t value = (uint32_t)value_raw;
    uint32_t masked = compute_masked_value((uint32_t)id, (uint32_t)n, value, mask_key);

    int fd = connect_with_retry(aggregator, port, 120, 100, (uint32_t)id);
    if (fd < 0) {
        fprintf(stderr, "[meter] connect failed id=%d aggregator=%s port=%d\n",
                id, aggregator, port);
        return 1;
    }

    secure_agg_packet_t pkt;
    pkt.magic = SECURE_AGG_MAGIC;
    pkt.version = SECURE_AGG_VERSION;
    pkt.type = SECURE_AGG_TYPE_SUBMISSION;
    pkt.test_id = test_id;
    pkt.id = (uint32_t)id;
    pkt.masked_value = masked;

    uint8_t wire[SECURE_AGG_WIRE_SIZE];
    secure_agg_pack(wire, &pkt);

    if (send_full(fd, wire, sizeof(wire)) != 0) {
        fprintf(stderr, "[meter] send failed id=%d\n", id);
        close(fd);
        return 1;
    }

    close(fd);

    double t1 = now_ms();

    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);

    printf("[meter] ID=%d\n", id);
    printf("[meter] MASKED_SUBMISSION=%u\n", masked);
    printf("[meter] SUBMIT_MS=%.3f\n", t1 - t0);
    printf("[meter] MAX_RSS_KB=%ld\n", ru.ru_maxrss);
    printf("[meter] STATUS=PASS\n");

    return 0;
}
