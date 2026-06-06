/*
 * mpc_share.c  --  Additive secret sharing over Fp
 */

#include "mpc_share.h"

void share_split(field_t s, int n, field_t *shares) {
    /*
     * Generate n-1 random shares, then set the last share so the sum is s.
     *
     * Correctness:
     *   shares[n-1] = s - (shares[0] + ... + shares[n-2])  mod p
     *   => sum of all n shares = s  mod p  (by construction)
     */
    field_t running_sum = 0;
    int i;

    for (i = 0; i < n - 1; i++) {
        shares[i]    = field_rand();
        running_sum  = field_add(running_sum, shares[i]);
    }

    /* Last share: s minus everything we already generated */
    shares[n - 1] = field_sub(s, running_sum);
}

field_t share_reconstruct(const field_t *shares, int n) {
    /*
     * Additive reconstruction: just sum all shares mod p.
     * No Lagrange interpolation, no field_inv.
     */
    field_t total = 0;
    int i;

    for (i = 0; i < n; i++) {
        total = field_add(total, shares[i]);
    }

    return total;
}
