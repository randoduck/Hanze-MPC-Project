#ifndef SECURE_AGG_H
#define SECURE_AGG_H

/*
 * secure_agg.h  --  Shared wire format for the central secure aggregation path.
 *
 * Used by both secure_agg_meter (client) and secure_agg_server (aggregator).
 * Keeping the packet definition in one header stops the two sides from
 * drifting apart.
 *
 * The packet is versioned and carries a per-run test_id so that a stale client
 * from a previous run cannot accidentally contaminate a new server's
 * aggregate. This is NOT authentication -- it only guards against accidents.
 */

#include <stdint.h>

#define SECURE_AGG_MAGIC            0x4D504341u   /* "MPCA" */
#define SECURE_AGG_VERSION         1
#define SECURE_AGG_TYPE_SUBMISSION 1

/*
 * Fixed 20-byte big-endian wire layout. We serialize field-by-field rather
 * than send the struct directly, so struct padding and host endianness can
 * never change the bytes on the wire.
 *
 *   offset  size  field
 *   0       4     magic
 *   4       2     version
 *   6       2     type
 *   8       4     test_id
 *   12      4     id
 *   16      4     masked_value
 */
#define SECURE_AGG_WIRE_SIZE 20

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t test_id;
    uint32_t id;
    uint32_t masked_value;
} secure_agg_packet_t;

static inline void sa_put_u32(uint8_t *b, uint32_t v) {
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)(v);
}

static inline void sa_put_u16(uint8_t *b, uint16_t v) {
    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t)(v);
}

static inline uint32_t sa_get_u32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
}

static inline uint16_t sa_get_u16(const uint8_t *b) {
    return (uint16_t)(((uint16_t)b[0] << 8) | (uint16_t)b[1]);
}

static inline void secure_agg_pack(uint8_t *buf, const secure_agg_packet_t *p) {
    sa_put_u32(buf + 0,  p->magic);
    sa_put_u16(buf + 4,  p->version);
    sa_put_u16(buf + 6,  p->type);
    sa_put_u32(buf + 8,  p->test_id);
    sa_put_u32(buf + 12, p->id);
    sa_put_u32(buf + 16, p->masked_value);
}

static inline void secure_agg_unpack(const uint8_t *buf, secure_agg_packet_t *p) {
    p->magic        = sa_get_u32(buf + 0);
    p->version      = sa_get_u16(buf + 4);
    p->type         = sa_get_u16(buf + 6);
    p->test_id      = sa_get_u32(buf + 8);
    p->id           = sa_get_u32(buf + 12);
    p->masked_value = sa_get_u32(buf + 16);
}

#endif /* SECURE_AGG_H */
