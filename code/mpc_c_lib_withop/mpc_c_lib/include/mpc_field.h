#ifndef MPC_FIELD_H
#define MPC_FIELD_H

/*
 * mpc_field.h  --  Finite field arithmetic over Fp
 *
 * Prime:  p = 2^31 - 1 = 2147483647  (Mersenne prime)
 *
 * Why this prime?
 *   - Fits in 32 bits; the product of two elements fits in 64 bits,
 *     so field_mul never overflows on 32-bit ARM (Cortex-A7 / Cortex-M4).
 *   - Far larger than any realistic smart-meter aggregate
 *     (20 parties * 1e6 Wh = 2e7 << 2.1e9).
 *   - Mersenne form allows fast reduction if needed later.
 *
 * mpyc correspondence:
 *   This module replaces mpyc/finfields.py (GF(p) arithmetic) and
 *   the gmpy2 invert() call used for Shamir reconstruction.
 */

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  Prime and element type                                              */
/* ------------------------------------------------------------------ */

#define MPC_PRIME  ((uint64_t)2147483647ULL)   /* 2^31 - 1 */

typedef uint32_t field_t;   /* one element of Fp; always in [0, p-1] */


/* ------------------------------------------------------------------ */
/*  Arithmetic                                                          */
/* ------------------------------------------------------------------ */

/*
 * field_add(a, b)  --  (a + b) mod p
 * mpyc: finfields.__add__
 *
 * Safe for 32-bit: a + b < 2p < 2^32, so uint64_t intermediate not needed.
 */
field_t field_add(field_t a, field_t b);

/*
 * field_sub(a, b)  --  (a - b) mod p
 * mpyc: finfields.__sub__
 *
 * Avoids signed arithmetic: adds p before subtracting when a < b.
 */
field_t field_sub(field_t a, field_t b);

/*
 * field_mul(a, b)  --  (a * b) mod p
 * mpyc: finfields.__mul__
 *
 * CRITICAL: widens to uint64_t before multiplying.
 * Without this, the product silently overflows on every 32-bit device.
 * Cortex-A7 executes the 64-bit UMULL in a single hardware instruction.
 */
field_t field_mul(field_t a, field_t b);

/*
 * field_inv(a)  --  a^{-1} mod p  (extended Euclidean algorithm)
 * mpyc: gmpy2.invert
 *
 * Returns 0 when a == 0 (undefined; caller must check).
 *
 * NOTE: Not required for v1 use cases (UC-1 through UC-5).
 * Additive sharing reconstruction is a plain sum -- no inverse needed.
 * Implement and test now; needed if Shamir threshold (t > 1) is added in v2.
 */
field_t field_inv(field_t a);

/*
 * field_rand()  --  uniform random element of Fp
 * mpyc: secrets / finfields random selection
 *
 * On Linux / Raspberry Pi: uses getrandom(2) (available since kernel 3.17;
 *   Raspbian bookworm ships 6.12).
 * On embedded without hardware RNG: replace with AES-CTR seeded from ADC
 *   noise (see MPC_UseCase_Analysis.docx Section 6).
 *
 * Uses rejection sampling so the distribution is exactly uniform over [0, p-1].
 * Rejection probability: 2 / 2^32 ~= 5e-10 per call -- negligible.
 */
field_t field_rand(void);

#endif /* MPC_FIELD_H */
