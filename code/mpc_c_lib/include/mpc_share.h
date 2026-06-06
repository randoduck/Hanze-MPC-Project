#ifndef MPC_SHARE_H
#define MPC_SHARE_H

/*
 * mpc_share.h  --  Additive secret sharing over Fp
 *
 * All v1 use cases (UC-1 through UC-5) use additive (n-out-of-n) sharing.
 * Shamir threshold sharing (t-out-of-n, t < n) is deferred to v2 and
 * would additionally require field_inv via Lagrange interpolation.
 *
 * mpyc correspondence:
 *   share_split       -> thresha.random_split
 *   share_reconstruct -> thresha.recombine  (additive variant)
 */

#include "mpc_field.h"

/*
 * share_split(s, n, shares)
 *
 * Split secret s in [0, p-1] into n additive shares.
 * After the call: sum(shares[0..n-1]) == s  (mod p).
 *
 * Algorithm:
 *   - shares[0..n-2]  <-- n-1 independent uniform random field elements
 *   - shares[n-1]     <-- (s - sum(shares[0..n-2])) mod p
 *
 * Preconditions:
 *   - shares[] must have room for at least n elements.
 *   - n >= 2.
 *   - s must be in [0, p-1].
 */
void share_split(field_t s, int n, field_t *shares);

/*
 * share_reconstruct(shares, n)
 *
 * Reconstruct the secret from n additive shares.
 * Returns sum(shares[0..n-1]) mod p.
 *
 * For additive sharing this is a plain modular sum -- field_inv is NOT used.
 * Only when Shamir (t > 1) is added in v2 does reconstruction need field_inv.
 */
field_t share_reconstruct(const field_t *shares, int n);

#endif /* MPC_SHARE_H */
