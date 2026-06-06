/*
 * mpc_field.c  --  Finite field arithmetic over Fp, p = 2^31 - 1
 *
 * All functions assume inputs are already reduced (i.e. in [0, p-1]).
 * Outputs are always reduced.
 */

#include "mpc_field.h"
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Platform-specific RNG                                               */
/* ------------------------------------------------------------------ */

#ifdef __linux__
#  include <sys/random.h>   /* getrandom(2) -- Linux 3.17+ */
#else
/*
 * Embedded fallback: a simple LCG.
 * REPLACE this with AES-CTR seeded from ADC noise on production hardware.
 * See MPC_UseCase_Analysis.docx Section 6 (Molina-Markham constraints).
 *
 * To seed: call mpc_field_seed_rng(adc_noise_value) once at startup.
 */
static uint32_t _rng_state = 0xdeadbeefUL;

void mpc_field_seed_rng(uint32_t seed) { _rng_state = seed; }

static uint32_t _lcg_next(void) {
    /* Numerical Recipes LCG -- good enough as a placeholder */
    _rng_state = _rng_state * 1664525UL + 1013904223UL;
    return _rng_state;
}
#endif

/* ------------------------------------------------------------------ */
/*  Shorthand                                                           */
/* ------------------------------------------------------------------ */

#define P  MPC_PRIME   /* 2147483647 -- 2^31 - 1 */

/* ------------------------------------------------------------------ */
/*  Arithmetic                                                          */
/* ------------------------------------------------------------------ */

field_t field_add(field_t a, field_t b) {
    /*
     * a, b in [0, p-1]  =>  a + b in [0, 2p-2] < 2^32
     * A single comparison is enough; no 64-bit needed here.
     */
    uint32_t s = a + b;
    return (s >= (uint32_t)P) ? s - (uint32_t)P : s;
}

field_t field_sub(field_t a, field_t b) {
    /*
     * Branchless-friendly: if a >= b, return a - b directly.
     * Otherwise add p first to keep the result positive.
     */
    return (a >= b) ? (a - b) : (uint32_t)((uint64_t)a + P - b);
}

field_t field_mul(field_t a, field_t b) {
    /*
     * Widen BOTH operands to 64 bits before multiplying.
     * Max product: (p-1)^2 = ~4.6e18, fits in uint64_t (max ~1.8e19).
     * Cortex-A7: UMULL executes this in one cycle.
     */
    return (field_t)(((uint64_t)a * (uint64_t)b) % P);
}

field_t field_inv(field_t a) {
    /*
     * Extended Euclidean algorithm.
     *
     * Invariant: at each step  r = a * t  (mod p).
     * When r becomes 1, t is the inverse.
     *
     * Uses int64_t intermediates to handle the signed quotient arithmetic
     * safely; all values stay within [-p, p] during the computation.
     */
    if (a == 0) return 0;   /* undefined; caller must not pass 0 */

    int64_t t    = 0,          newt = 1;
    int64_t r    = (int64_t)P, newr = (int64_t)a;

    while (newr != 0) {
        int64_t q   = r / newr;
        int64_t tmp;

        tmp = t;    t    = newt;    newt = tmp - q * newt;
        tmp = r;    r    = newr;    newr = tmp - q * newr;
    }
    /* r is now gcd(a, p) = 1 (p is prime, a != 0) */

    if (t < 0) t += (int64_t)P;
    return (field_t)t;
}

field_t field_rand(void) {
    /*
     * Rejection sampling for an exactly uniform draw from [0, p-1].
     *
     * For p = 2^31 - 1:
     *   2 * p = 4294967294  (fits in uint32_t; UINT32_MAX = 4294967295)
     *   Reject only when val == 4294967294 or 4294967295.
     *   Rejection probability: 2 / 2^32 ~= 4.7e-10 -- negligible.
     *
     * After rejection, val is in [0, 2p-1].
     *   If val < p  =>  result = val
     *   If val >= p =>  result = val - p   (maps [p, 2p-1] -> [0, p-1])
     */
    uint32_t val;
    uint32_t two_p = (uint32_t)(2U * (uint32_t)P);   /* 4294967294 */

    do {
#ifdef __linux__
        (void)getrandom(&val, sizeof(val), 0);
#else
        val = _lcg_next();
#endif
    } while (val >= two_p);

    return (field_t)(val >= (uint32_t)P ? val - (uint32_t)P : val);
}
